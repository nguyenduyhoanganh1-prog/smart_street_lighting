/**
 * @file main.c
 * @brief streetlight-node: BH1750 + 4x INA219 + 4 PWM lamps, lighting rules,
 *        energy bookkeeping (NVS), SNTP time, MQTT telemetry and commands.
 *
 * Control cycle (every APP_SAMPLE_PERIOD_MS, in app_main):
 *   sensors -> clock -> lighting rules -> PWM -> energy -> P100 -> MQTT/NVS
 *
 * Not implemented yet: camera + FOMO zone rules (all lamps share one duty),
 * alerts (sensor errors are reported through state.sensor_error).
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "energy_acc.h"
#include "energy_stats.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lamp_pwm.h"
#include "lighting.h"
#include "node_store.h"
#include "nvs_flash.h"
#include "sensor_hub.h"
#include "sl_mqtt.h"
#include "sl_secrets.h"
#include "sl_wifi.h"
#include "time_sync.h"

#define TAG "node"

_Static_assert(APP_LAMP_COUNT == SL_LAMP_COUNT, "lamp count differs from the MQTT protocol");
_Static_assert(APP_LAMP_COUNT == ENERGY_ACC_CHANNELS, "lamp count differs from energy_acc");
_Static_assert(APP_LAMP_COUNT == ENERGY_LAMPS, "lamp count differs from energy_stats");
_Static_assert(ENERGY_HOURS == SL_HISTORY_HOURS, "history length differs from the MQTT protocol");

typedef struct {
    bool valid;              /* SNTP synced */
    int64_t epoch_s;
    uint16_t minute_of_day;  /* local */
    int32_t day_key;         /* local yyyymmdd */
} clock_info_t;

typedef struct {
    sl_config_t config;
    bool manual;                          /* user selected manual mode */
    int64_t manual_until_ms;
    uint8_t manual_duty[APP_LAMP_COUNT];
    bool force_night;
    bool night;                           /* effective night (sensor, clock or forced) */
    sl_mode_t mode;                       /* reported: auto / manual / schedule */
} node_state_t;

typedef struct {
    int64_t since_ms;                     /* -1 = lamp not at steady 100 % */
    double sum_mw;
    uint32_t samples;
} p100_cal_t;

static const char *const MODE_TEXT[] = {
    [SL_MODE_AUTO] = "auto",
    [SL_MODE_MANUAL] = "manual",
    [SL_MODE_SCHEDULE] = "schedule",
};

static const int LAMP_GPIOS[APP_LAMP_COUNT] = {
    APP_LAMP1_GPIO, APP_LAMP2_GPIO, APP_LAMP3_GPIO, APP_LAMP4_GPIO,
};

/* Large or long-lived state is static (no heap, small main-task stack use) */
static sl_secrets_t s_secrets;
static bool s_secrets_ok;
static bool s_nvs_ok;
static bool s_mqtt_started;
static bool s_time_was_valid;
static sensor_snapshot_t s_snap;
static energy_acc_t s_energy_acc;
static energy_stats_t s_stats;
static bool s_stats_dirty;
static daynight_t s_daynight;
static node_state_t s_node;
static p100_cal_t s_p100[APP_LAMP_COUNT];
static uint32_t s_hist_buf[APP_LAMP_COUNT][ENERGY_HOURS];
static int64_t s_last_telemetry_ms;
static bool s_history_due;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ================================== Clock ================================== */

static clock_info_t read_clock(void)
{
    clock_info_t clk = { 0 };
    if (!time_sync_is_valid()) {
        return clk;
    }
    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    clk.valid = true;
    clk.epoch_s = (int64_t)now;
    clk.minute_of_day = (uint16_t)(local.tm_hour * 60 + local.tm_min);
    clk.day_key = (int32_t)((local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday);
    return clk;
}

static void save_energy(const char *reason)
{
    if (!s_nvs_ok) {
        return;
    }
    const esp_err_t err = node_store_save_energy(&s_stats);
    if (err == ESP_OK) {
        s_stats_dirty = false;
        ESP_LOGI(TAG, "energy saved to NVS (%s)", reason);
    } else {
        ESP_LOGW(TAG, "energy save failed: %s", esp_err_to_name(err));
    }
}

/* Roll the hourly history and the daily counters when the clock moves on */
static void update_clock(const clock_info_t *clk)
{
    if (!clk->valid) {
        return;
    }
    if (!s_time_was_valid) {
        s_time_was_valid = true;
        s_history_due = true;
        ESP_LOGI(TAG, "clock synced: day %ld, %02u:%02u (UTC+7)", (long)clk->day_key,
                 (unsigned)(clk->minute_of_day / 60U), (unsigned)(clk->minute_of_day % 60U));
    }
    const unsigned flags = energy_stats_roll(&s_stats, clk->epoch_s, clk->day_key);
    if ((flags & ENERGY_ROLL_DAY) != 0U) {
        ESP_LOGI(TAG, "new day %ld: energy 'today' reset", (long)clk->day_key);
    }
    if (flags != 0U) {
        s_history_due = true;
        save_energy((flags & ENERGY_ROLL_DAY) != 0U ? "new day" : "new hour");
    }
}

/* ================================ Lighting ================================ */

static void update_lighting(const clock_info_t *clk, int64_t t_ms)
{
    const lighting_input_t in = {
        .now_ms = t_ms,
        .lux_ok = s_snap.lux_ok,
        .lux = s_snap.lux,
        .time_valid = clk->valid,
        .minute_of_day = clk->minute_of_day,
        .cfg = &s_node.config,
    };
    const bool sensor_night = daynight_update(&s_daynight, &in, APP_DAY_NIGHT_HOLD_MS);
    const bool clock_night = !clk->valid || lighting_in_night_window(clk->minute_of_day, &s_node.config);
    s_node.night = s_node.force_night || (s_snap.lux_ok ? sensor_night : clock_night);

    if (s_node.manual && t_ms >= s_node.manual_until_ms) {
        s_node.manual = false;
        ESP_LOGI(TAG, "manual mode timed out -> auto");
    }

    uint8_t duty[APP_LAMP_COUNT];
    if (s_node.manual) {
        s_node.mode = SL_MODE_MANUAL;
        memcpy(duty, s_node.manual_duty, sizeof(duty));
    } else {
        const lighting_decision_t d = lighting_decide(&in, s_node.night, APP_BASE_DIM_PCT, APP_FULL_PCT);
        s_node.mode = d.mode;
        memset(duty, d.duty, sizeof(duty));
    }
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        (void)lamp_pwm_set(k, duty[k], s_node.config.fade_up_ms, s_node.config.fade_down_ms);
    }
    lamp_pwm_service();
}

/* ================================= Energy ================================= */

static void update_energy(int64_t dt_us)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        if (!s_snap.ina_ok[k]) {
            energy_acc_skip(&s_energy_acc, k);
            continue;
        }
        const double mwh = energy_acc_add(&s_energy_acc, k, s_snap.ina[k].power_mw, s_snap.timestamp_us);
        if (mwh > 0.0) {
            energy_stats_add(&s_stats, k, mwh, s_node.night);
            s_stats_dirty = true;
        }
    }
    if (s_node.night && dt_us > 0 && dt_us <= ENERGY_ACC_MAX_GAP_US) {
        energy_stats_add_night_time(&s_stats, (double)dt_us / 1e6);
    }
}

/* Self-calibrate P100: average power of a lamp held at a steady 100 % */
static void update_p100(int64_t t_ms)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        p100_cal_t *c = &s_p100[k];
        const bool steady_full = lamp_pwm_target(k) == APP_FULL_PCT && lamp_pwm_is_steady(k)
                                 && s_snap.ina_ok[k];
        if (!steady_full) {
            c->since_ms = -1;
            continue;
        }
        if (c->since_ms < 0) {
            *c = (p100_cal_t){ .since_ms = t_ms };
            continue;
        }
        c->sum_mw += (double)s_snap.ina[k].power_mw;
        c->samples++;
        if (t_ms - c->since_ms < (int64_t)APP_P100_WINDOW_MS) {
            continue;
        }
        const float avg = (float)(c->sum_mw / (double)c->samples);
        const float old = s_stats.p100_mw[k];
        if (avg > 0.0f && (old <= 0.0f || fabsf(avg - old) > APP_P100_CHANGE_RATIO * old)) {
            s_stats.p100_mw[k] = avg;
            ESP_LOGI(TAG, "lamp %u: P100 calibrated = %.1f mW", k + 1U, (double)avg);
            save_energy("P100");
        }
        *c = (p100_cal_t){ .since_ms = t_ms };
    }
}

/* ================================== Log ================================== */

static void log_status(const clock_info_t *clk)
{
    if (clk->valid) {
        ESP_LOGI(TAG, "%02u:%02u | mode=%s | %s%s", (unsigned)(clk->minute_of_day / 60U),
                 (unsigned)(clk->minute_of_day % 60U), MODE_TEXT[s_node.mode],
                 s_node.night ? "NIGHT" : "DAY", s_node.force_night ? " (forced)" : "");
    } else {
        ESP_LOGI(TAG, "--:-- (no SNTP) | mode=%s | %s%s", MODE_TEXT[s_node.mode],
                 s_node.night ? "NIGHT" : "DAY", s_node.force_night ? " (forced)" : "");
    }
    if (s_snap.lux_ok) {
        ESP_LOGI(TAG, "BH1750     lux=%.1f", (double)s_snap.lux);
    } else {
        ESP_LOGW(TAG, "BH1750     offline -> schedule mode");
    }
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        const unsigned duty = lamp_pwm_get(k);
        if (!s_snap.ina_ok[k]) {
            ESP_LOGW(TAG, "Lamp %u  duty=%3u%%  INA219 offline", k + 1U, duty);
            continue;
        }
        const ina219_sample_t *s = &s_snap.ina[k];
        ESP_LOGI(TAG, "Lamp %u  duty=%3u%%  V=%.3f V  I=%6.2f mA  P=%6.1f mW  today=%.1f total=%.1f mWh",
                 k + 1U, duty, (double)s->bus_v, (double)s->current_ma, (double)s->power_mw,
                 s_stats.today_mwh[k], s_stats.total_mwh[k]);
    }
    ESP_LOGI(TAG, "WiFi %s | MQTT %s", sl_wifi_is_connected() ? "up" : "down",
             sl_mqtt_is_connected() ? "connected" : "not connected");
}

/* ================================== MQTT ================================== */

static bool publish_ok(const char *what, esp_err_t err)
{
    if (err == ESP_OK) {
        return true;
    }
    if (err != ESP_ERR_INVALID_STATE) { /* INVALID_STATE = not connected, expected */
        ESP_LOGW(TAG, "publish %s failed: %s", what, esp_err_to_name(err));
    }
    return false;
}

static void publish_history(void)
{
    if (s_stats.hist_hour < 0) {
        return; /* no clock yet: hours are unknown */
    }
    energy_stats_history(&s_stats, s_hist_buf);
    const esp_err_t err = sl_mqtt_publish_history(s_stats.hist_hour * 3600,
                                                  (const uint32_t (*)[SL_HISTORY_HOURS])s_hist_buf);
    if (publish_ok("history", err)) {
        s_history_due = false;
        ESP_LOGI(TAG, "MQTT -> streetlight/energy/history");
    }
}

static void publish_telemetry(void)
{
    uint8_t duty[APP_LAMP_COUNT];
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        duty[k] = lamp_pwm_get(k);
    }
    /* persons, vehicles, zones and fps stay 0 until the camera + AI exist */
    const sl_state_t state = {
        .night = s_node.night,
        .lux = s_snap.lux_ok ? (int32_t)lroundf(s_snap.lux) : 0,
        .mode = s_node.mode,
        .force_night = s_node.force_night,
        .duty = { duty[0], duty[1], duty[2], duty[3] },
        .sensor_error = s_snap.lux_ok ? NULL : "bh1750",
    };

    sl_lamp_energy_t lamps[SL_LAMP_COUNT];
    for (unsigned k = 0; k < SL_LAMP_COUNT; k++) {
        const ina219_sample_t *s = &s_snap.ina[k];
        lamps[k] = (sl_lamp_energy_t){
            .duty = duty[k],
            .ina_ok = s_snap.ina_ok[k],
            .v_volt = s->bus_v,
            .i_ma = s->current_ma,
            .p_mw = s->power_mw,
            .e_today_mwh = (uint32_t)llround(s_stats.today_mwh[k]),
            .e_total_mwh = (uint32_t)llround(s_stats.total_mwh[k]),
            .saving_pct = energy_stats_saving_pct(&s_stats, k),
            .fault = -1, /* let the dashboard detect faults from v/i/duty */
        };
    }

    const bool state_sent = publish_ok("state", sl_mqtt_publish_state(&state));
    const bool energy_sent = publish_ok("energy", sl_mqtt_publish_energy(lamps));
    if (state_sent && energy_sent) {
        ESP_LOGI(TAG, "MQTT -> streetlight/state + streetlight/energy");
    }
    if (s_history_due) {
        publish_history();
    }
}

static void apply_config_command(const sl_config_patch_t *patch)
{
    sl_config_t next;
    sl_codec_apply_config(&s_node.config, patch, &next);
    if (memcmp(&next, &s_node.config, sizeof(next)) != 0) {
        s_node.config = next;
        /* Write NVS only when something really changed (flash wear) */
        if (s_nvs_ok) {
            const esp_err_t err = node_store_save_config(&s_node.config);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "config save failed: %s", esp_err_to_name(err));
            }
        }
    }
    ESP_LOGI(TAG, "config: lux %u/%u, fade %u/%u ms, night %s-%s",
             (unsigned)s_node.config.lux_night, (unsigned)s_node.config.lux_day,
             (unsigned)s_node.config.fade_up_ms, (unsigned)s_node.config.fade_down_ms,
             s_node.config.night_start, s_node.config.night_end);
    /* Always echo: the dashboard shows the values the board really uses */
    (void)publish_ok("config", sl_mqtt_publish_config(&s_node.config));
}

static void handle_command(const sl_cmd_t *cmd)
{
    const int64_t t_ms = now_ms();
    switch (cmd->type) {
    case SL_CMD_MODE:
        if (cmd->u.mode == SL_MODE_MANUAL) {
            /* Start manual mode from what the lamps are doing now */
            for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
                s_node.manual_duty[k] = lamp_pwm_target(k);
            }
            s_node.manual = true;
            s_node.manual_until_ms = t_ms + (int64_t)APP_MANUAL_TIMEOUT_MS;
        } else {
            s_node.manual = false;
        }
        ESP_LOGI(TAG, "cmd: mode -> %s", s_node.manual ? "manual" : "auto");
        break;
    case SL_CMD_LAMP:
        if (!s_node.manual) {
            ESP_LOGW(TAG, "cmd: lamp %u ignored, switch to manual mode first",
                     (unsigned)cmd->u.lamp.lamp);
            break;
        }
        s_node.manual_duty[cmd->u.lamp.lamp - 1U] = cmd->u.lamp.duty;
        s_node.manual_until_ms = t_ms + (int64_t)APP_MANUAL_TIMEOUT_MS;
        ESP_LOGI(TAG, "cmd: lamp %u -> %u%%", (unsigned)cmd->u.lamp.lamp, (unsigned)cmd->u.lamp.duty);
        break;
    case SL_CMD_FORCE_NIGHT:
        s_node.force_night = cmd->u.force_night;
        ESP_LOGI(TAG, "cmd: force_night -> %s", s_node.force_night ? "true" : "false");
        break;
    case SL_CMD_ALERT_ACK:
        ESP_LOGI(TAG, "cmd: alert '%s' acknowledged", cmd->u.alert_id);
        (void)publish_ok("alert clear", sl_mqtt_clear_alert());
        break;
    case SL_CMD_CONFIG:
        apply_config_command(&cmd->u.config);
        break;
    default:
        break;
    }
    s_last_telemetry_ms = 0; /* push the new state on the next cycle */
}

static void drain_mqtt_events(void)
{
    sl_mqtt_event_t ev;
    while (sl_mqtt_wait_event(&ev, 0)) {
        switch (ev.type) {
        case SL_MQTT_EVT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected, publishing config + history");
            (void)publish_ok("config", sl_mqtt_publish_config(&s_node.config));
            s_history_due = true;
            s_last_telemetry_ms = 0;
            break;
        case SL_MQTT_EVT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case SL_MQTT_EVT_COMMAND:
            handle_command(&ev.cmd);
            break;
        default:
            break;
        }
    }
}

/* ================================ Network ================================ */

static void start_wifi(void)
{
    esp_err_t err = sl_secrets_load(&s_secrets);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no WiFi/MQTT secrets in NVS -> offline mode (no SNTP, no MQTT)");
        return;
    }
    s_secrets_ok = true;
    err = sl_wifi_start(s_secrets.wifi_ssid, s_secrets.wifi_pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sl_wifi_start failed: %s", esp_err_to_name(err));
    }
}

/* Start SNTP and MQTT only once WiFi has an IP */
static void start_network_services(void)
{
    if (!s_secrets_ok || !sl_wifi_is_connected()) {
        return;
    }
    (void)time_sync_start(); /* no-op after the first successful call */
    if (s_mqtt_started) {
        return;
    }
    s_mqtt_started = true; /* one attempt; esp-mqtt reconnects by itself afterwards */
    const sl_mqtt_config_t cfg = {
        .uri = s_secrets.mqtt_uri,
        .username = s_secrets.mqtt_user,
        .password = s_secrets.mqtt_pass,
    };
    const esp_err_t err = sl_mqtt_start(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started -> %s", s_secrets.mqtt_uri);
    } else {
        ESP_LOGE(TAG, "sl_mqtt_start failed: %s", esp_err_to_name(err));
    }
}

/* ================================== Init ================================== */

static void load_persistent_state(void)
{
    esp_err_t err = node_store_load_config(&s_node.config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "config loaded from NVS");
    } else {
        ESP_LOGI(TAG, "no stored config (%s), using defaults", esp_err_to_name(err));
    }
    err = node_store_load_energy(&s_stats);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "energy loaded from NVS");
    } else {
        ESP_LOGI(TAG, "no stored energy (%s), starting from 0", esp_err_to_name(err));
    }
}

static void init_node(void)
{
    time_sync_set_timezone();
    s_node.config = SL_CONFIG_DEFAULT;
    s_node.mode = SL_MODE_AUTO;
    energy_acc_init(&s_energy_acc);
    energy_stats_init(&s_stats);
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        s_p100[k].since_ms = -1;
    }

    esp_err_t err = lamp_pwm_init(LAMP_GPIOS, APP_LAMP_COUNT, APP_PWM_FREQ_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lamp PWM init failed: %s (check APP_LAMPx_GPIO)", esp_err_to_name(err));
    }
    if (sensor_hub_init() != ESP_OK) {
        ESP_LOGE(TAG, "sensor bus not available, check APP_I2C_* in app_config.h");
    }

    /* Never erase NVS automatically: it holds the WiFi/MQTT secrets */
    err = nvs_flash_init();
    s_nvs_ok = (err == ESP_OK);
    if (s_nvs_ok) {
        load_persistent_state();
        start_wifi();
    } else {
        ESP_LOGE(TAG, "nvs_flash_init: %s -> no persistence, no network", esp_err_to_name(err));
    }
}

/* ================================== Main ================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "streetlight-node starting");
    init_node();

    int64_t last_log_ms = 0;
    int64_t last_save_ms = now_ms();
    int64_t last_cycle_us = esp_timer_get_time();
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        const int64_t cycle_us = esp_timer_get_time();
        const int64_t t_ms = cycle_us / 1000;

        sensor_hub_sample(&s_snap);
        const clock_info_t clk = read_clock();
        update_clock(&clk);
        update_lighting(&clk, t_ms);
        update_energy(cycle_us - last_cycle_us);
        update_p100(t_ms);
        last_cycle_us = cycle_us;

        start_network_services();
        drain_mqtt_events();

        if (t_ms - last_log_ms >= (int64_t)APP_LOG_PERIOD_MS) {
            last_log_ms = t_ms;
            log_status(&clk);
        }
        if (sl_mqtt_is_connected() && t_ms - s_last_telemetry_ms >= (int64_t)APP_TELEMETRY_PERIOD_MS) {
            s_last_telemetry_ms = t_ms;
            publish_telemetry();
        }
        if (s_stats_dirty && t_ms - last_save_ms >= (int64_t)APP_ENERGY_SAVE_PERIOD_MS) {
            last_save_ms = t_ms;
            save_energy("periodic");
        }
        (void)xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}
