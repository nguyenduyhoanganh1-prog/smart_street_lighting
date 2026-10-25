/**
 * @file node_ctrl.c
 * @brief Control core of tune-by-wifi. Logic mirrors streetlight-node/main/main.c
 *        (lighting rules, energy bookkeeping, P100 self-calibration) minus MQTT/SNTP.
 *
 * Locking: s_lock guards every static below. node_ctrl_step() reads the sensors
 * (slow, blocking I2C) BEFORE taking the lock, so the HTTP task is never held up
 * by the bus.
 */
#include "node_ctrl.h"

#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "energy_acc.h"
#include "energy_stats.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lamp_pwm.h"
#include "lighting.h"
#include "node_store.h"
#include "nvs_flash.h"
#include "sensor_hub.h"
#include "time_sync.h"

#define TAG "node_ctrl"

#define MIN_VALID_EPOCH_S 1700000000LL /* Nov 2023: anything older is a wrong browser clock */
#define MAX_CLOCK_JITTER_S 5           /* do not step the clock for smaller differences */

typedef struct {
    bool valid;
    int64_t epoch_s;
    uint16_t minute_of_day;
    int32_t day_key;
} clock_info_t;

typedef struct {
    sl_config_t config;
    bool manual;
    int64_t manual_until_ms;
    uint8_t manual_duty[APP_LAMP_COUNT];
    bool force_night;
    bool night;
    sl_mode_t mode;
} node_state_t;

typedef struct {
    int64_t since_ms; /* -1 = lamp not at steady 100 % */
    double sum_mw;
    uint32_t samples;
} p100_cal_t;

static const int LAMP_GPIOS[APP_LAMP_COUNT] = {
    APP_LAMP1_GPIO, APP_LAMP2_GPIO, APP_LAMP3_GPIO, APP_LAMP4_GPIO,
};

static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static bool s_nvs_ok;
static bool s_time_was_valid;
static sensor_snapshot_t s_snap;
static energy_acc_t s_energy_acc;
static energy_stats_t s_stats;
static bool s_stats_dirty;
static daynight_t s_daynight;
static node_state_t s_node;
static p100_cal_t s_p100[APP_LAMP_COUNT];
static node_status_t s_status;
static clock_info_t s_clock;
static int64_t s_last_cycle_us;
static int64_t s_last_save_ms;
static int64_t s_t_ms; /* time of the last step, for manual countdown */

static void lock(void)
{
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    (void)xSemaphoreGive(s_lock);
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

static void update_clock(const clock_info_t *clk)
{
    if (!clk->valid) {
        return;
    }
    if (!s_time_was_valid) {
        s_time_was_valid = true;
        ESP_LOGI(TAG, "clock set: day %ld, %02u:%02u (UTC+7)", (long)clk->day_key,
                 (unsigned)(clk->minute_of_day / 60U), (unsigned)(clk->minute_of_day % 60U));
    }
    const unsigned flags = energy_stats_roll(&s_stats, clk->epoch_s, clk->day_key);
    if (flags != 0U) {
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
    /* Manual mode follows the slider instantly: a fade here is felt as lag */
    const uint32_t fade_up_ms = s_node.manual ? 0U : s_node.config.fade_up_ms;
    const uint32_t fade_down_ms = s_node.manual ? 0U : s_node.config.fade_down_ms;
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        (void)lamp_pwm_set(k, duty[k], fade_up_ms, fade_down_ms);
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

/* ================================== Status ================================== */

/* Rebuild the snapshot read by the web page. Caller holds the lock. */
static void refresh_status(void)
{
    node_status_t *s = &s_status;
    s->night = s_node.night;
    s->force_night = s_node.force_night;
    s->mode = s_node.mode;
    s->manual_left_s = (s_node.manual && s_node.manual_until_ms > s_t_ms)
                           ? (uint32_t)((s_node.manual_until_ms - s_t_ms) / 1000)
                           : 0U;
    s->lux_ok = s_snap.lux_ok;
    s->lux = s_snap.lux;
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        s->duty[k] = lamp_pwm_get(k);
        s->ina_ok[k] = s_snap.ina_ok[k];
        s->v_volt[k] = s_snap.ina[k].bus_v;
        s->i_ma[k] = s_snap.ina[k].current_ma;
        s->p_mw[k] = s_snap.ina[k].power_mw;
        s->e_today_mwh[k] = s_stats.today_mwh[k];
        s->e_total_mwh[k] = s_stats.total_mwh[k];
        s->saving_pct[k] = energy_stats_saving_pct(&s_stats, k);
    }
    s->time_valid = s_clock.valid;
    s->minute_of_day = s_clock.minute_of_day;
    s->uptime_s = (uint32_t)(s_t_ms / 1000);
    s->config = s_node.config;
}

void node_ctrl_get_status(node_status_t *out)
{
    lock();
    *out = s_status;
    unlock();
}

_Static_assert(APP_LAMP_COUNT == ENERGY_LAMPS, "lamp count differs from energy_stats");
_Static_assert(ENERGY_HOURS == SL_HISTORY_HOURS, "history length differs from the MQTT protocol");

bool node_ctrl_get_history(uint32_t out[APP_LAMP_COUNT][SL_HISTORY_HOURS], int64_t *t_sec)
{
    if (out == NULL || t_sec == NULL) {
        return false;
    }
    lock();
    const bool valid = s_stats.hist_hour >= 0;
    if (valid) {
        energy_stats_history(&s_stats, out);
        *t_sec = s_stats.hist_hour * 3600;
    }
    unlock();
    return valid;
}

/* ================================== Control ================================== */

void node_ctrl_step(void)
{
    sensor_snapshot_t snap;
    sensor_hub_sample(&snap); /* blocking I2C: outside the lock */

    const int64_t cycle_us = esp_timer_get_time();
    const int64_t t_ms = cycle_us / 1000;

    lock();
    s_snap = snap;
    s_t_ms = t_ms;
    s_clock = read_clock();
    update_clock(&s_clock);
    update_lighting(&s_clock, t_ms);
    update_energy(cycle_us - s_last_cycle_us);
    update_p100(t_ms);
    s_last_cycle_us = cycle_us;
    if (s_stats_dirty && t_ms - s_last_save_ms >= (int64_t)APP_ENERGY_SAVE_PERIOD_MS) {
        s_last_save_ms = t_ms;
        save_energy("periodic");
    }
    refresh_status();
    unlock();
}

esp_err_t node_ctrl_apply_cmd(const sl_cmd_t *cmd)
{
    if (cmd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_OK;
    lock();
    switch (cmd->type) {
    case SL_CMD_MODE:
        if (cmd->u.mode == SL_MODE_MANUAL) {
            for (unsigned k = 0; k < APP_LAMP_COUNT; k++) { /* start from what the lamps do now */
                s_node.manual_duty[k] = lamp_pwm_target(k);
            }
            s_node.manual = true;
            s_node.manual_until_ms = s_t_ms + (int64_t)APP_MANUAL_TIMEOUT_MS;
            s_node.mode = SL_MODE_MANUAL;
        } else {
            s_node.manual = false;
        }
        ESP_LOGI(TAG, "cmd: mode -> %s", s_node.manual ? "manual" : "auto");
        break;
    case SL_CMD_LAMP:
        if (!s_node.manual) {
            ret = ESP_ERR_INVALID_STATE; /* switch to manual first */
        } else if (cmd->u.lamp.lamp >= 1U && cmd->u.lamp.lamp <= APP_LAMP_COUNT) {
            const unsigned idx = cmd->u.lamp.lamp - 1U;
            s_node.manual_duty[idx] = cmd->u.lamp.duty;
            s_node.manual_until_ms = s_t_ms + (int64_t)APP_MANUAL_TIMEOUT_MS;
            /* Apply now instead of waiting for the next control cycle (up to ~200 ms) */
            (void)lamp_pwm_set(idx, cmd->u.lamp.duty, 0U, 0U);
            ESP_LOGI(TAG, "cmd: lamp %u -> %u%%", (unsigned)cmd->u.lamp.lamp, (unsigned)cmd->u.lamp.duty);
        } else {
            ret = ESP_ERR_INVALID_ARG;
        }
        break;
    case SL_CMD_FORCE_NIGHT:
        s_node.force_night = cmd->u.force_night;
        s_node.night = s_node.force_night || s_node.night;
        ESP_LOGI(TAG, "cmd: force_night -> %s", s_node.force_night ? "true" : "false");
        break;
    case SL_CMD_CONFIG: {
        sl_config_t next;
        sl_codec_apply_config(&s_node.config, &cmd->u.config, &next);
        if (memcmp(&next, &s_node.config, sizeof(next)) != 0) {
            s_node.config = next;
            if (s_nvs_ok) { /* write only on change: flash wear */
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
        break;
    }
    default:
        ret = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    if (ret == ESP_OK) {
        refresh_status(); /* the page re-reads right after the POST */
    }
    unlock();
    return ret;
}

esp_err_t node_ctrl_set_time(int64_t epoch_s)
{
    if (epoch_s < MIN_VALID_EPOCH_S) {
        return ESP_ERR_INVALID_ARG;
    }
    if (time_sync_is_valid()) {
        const int64_t diff = (int64_t)time(NULL) - epoch_s;
        if (diff >= -MAX_CLOCK_JITTER_S && diff <= MAX_CLOCK_JITTER_S) {
            return ESP_OK;
        }
    }
    const struct timeval tv = { .tv_sec = (time_t)epoch_s, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "clock set from browser: %lld", (long long)epoch_s);
    return ESP_OK;
}

/* ================================== Init ================================== */

esp_err_t node_ctrl_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    time_sync_set_timezone();
    s_node.config = SL_CONFIG_DEFAULT;
    s_node.mode = SL_MODE_AUTO;
    energy_acc_init(&s_energy_acc);
    energy_stats_init(&s_stats);
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        s_p100[k].since_ms = -1;
    }

    esp_err_t err = lamp_pwm_init(LAMP_GPIOS, APP_LAMP_COUNT, APP_PWM_FREQ_HZ, APP_LAMP_ACTIVE_LOW != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lamp PWM init failed: %s (check APP_LAMPx_GPIO)", esp_err_to_name(err));
    }
    if (sensor_hub_init() != ESP_OK) {
        ESP_LOGE(TAG, "sensor bus not available, check APP_I2C_* in app_config.h");
    }

    /* Never erase NVS automatically: it holds the config, energy and (in streetlight-node) secrets */
    err = nvs_flash_init();
    s_nvs_ok = (err == ESP_OK);
    if (s_nvs_ok) {
        if (node_store_load_config(&s_node.config) == ESP_OK) {
            ESP_LOGI(TAG, "config loaded from NVS");
        }
        if (node_store_load_energy(&s_stats) == ESP_OK) {
            ESP_LOGI(TAG, "energy loaded from NVS");
        }
    } else {
        ESP_LOGE(TAG, "nvs_flash_init: %s -> settings will not be saved", esp_err_to_name(err));
    }
    s_last_cycle_us = esp_timer_get_time();
    s_last_save_ms = s_last_cycle_us / 1000;
    lock();
    refresh_status();
    unlock();
    return err;
}
