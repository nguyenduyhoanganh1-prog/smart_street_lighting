/**
 * @file mqtt_link.c
 * @brief MQTT telemetry and commands for streetlight-wifi (see mqtt_link.h).
 *
 * Runs in the main task, so node_ctrl_step() and the publishes never overlap. The slow
 * TLS work happens in the esp-mqtt task; sl_mqtt_publish_*() only queues the message.
 * Topics and payloads are the ones of the dashboard (plans/00 "MQTT topics").
 */
#include "mqtt_link.h"

#include <math.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "node_ctrl.h"
#include "sl_mqtt.h"
#include "sl_secrets.h"
#include "time_sync.h"
#include "wifi_ap.h"

#define TAG "mqtt_link"

_Static_assert(APP_LAMP_COUNT == SL_LAMP_COUNT, "lamp count differs from the MQTT protocol");

static sl_secrets_t s_secrets;
static bool s_secrets_ok;
static bool s_mqtt_started;
static bool s_history_due;
static int64_t s_last_telemetry_ms;
static uint32_t s_hist_buf[APP_LAMP_COUNT][SL_HISTORY_HOURS];

bool mqtt_link_init(void)
{
    s_secrets_ok = (sl_secrets_load(&s_secrets) == ESP_OK);
    if (!s_secrets_ok) {
        ESP_LOGW(TAG, "no WiFi/MQTT secrets in NVS -> web page only (see secrets.example.csv)");
    }
    return s_secrets_ok;
}

const char *mqtt_link_wifi_ssid(void)
{
    return s_secrets_ok ? s_secrets.wifi_ssid : "";
}

const char *mqtt_link_wifi_pass(void)
{
    return s_secrets_ok ? s_secrets.wifi_pass : "";
}

/* ================================ Publishing ================================ */

/* INVALID_STATE just means "broker not connected yet": not worth a warning */
static bool publish_ok(const char *what, esp_err_t err)
{
    if (err == ESP_OK) {
        return true;
    }
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "publish %s failed: %s", what, esp_err_to_name(err));
    }
    return false;
}

static void publish_config(const node_status_t *st)
{
    (void)publish_ok("config", sl_mqtt_publish_config(&st->config));
}

static void publish_history(void)
{
    int64_t t_sec = 0;
    if (!node_ctrl_get_history(s_hist_buf, &t_sec)) {
        return; /* no clock yet: the hours are unknown */
    }
    if (publish_ok("history", sl_mqtt_publish_history(t_sec, (const uint32_t (*)[SL_HISTORY_HOURS])s_hist_buf))) {
        s_history_due = false;
    }
}

static void publish_telemetry(const node_status_t *st)
{
    /* persons, vehicles, zones and fps stay 0: this project has no camera/AI */
    sl_state_t state = {
        .night = st->night,
        .lux = st->lux_ok ? (int32_t)lroundf(st->lux) : 0,
        .mode = st->mode,
        .force_night = st->force_night,
        .sensor_error = st->lux_ok ? NULL : "bh1750",
    };
    sl_lamp_energy_t lamps[SL_LAMP_COUNT];
    for (unsigned k = 0; k < SL_LAMP_COUNT; k++) {
        state.duty[k] = st->duty[k];
        lamps[k] = (sl_lamp_energy_t){
            .duty = st->duty[k],
            .ina_ok = st->ina_ok[k],
            .v_volt = st->v_volt[k],
            .i_ma = st->i_ma[k],
            .p_mw = st->p_mw[k],
            .e_today_mwh = (uint32_t)llround(st->e_today_mwh[k]),
            .e_total_mwh = (uint32_t)llround(st->e_total_mwh[k]),
            .saving_pct = st->saving_pct[k],
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

/* ================================== Events ================================== */

static void handle_command(const sl_cmd_t *cmd)
{
    if (cmd->type == SL_CMD_ALERT_ACK) { /* no alerts without camera: just clear the topic */
        (void)publish_ok("alert clear", sl_mqtt_clear_alert());
        return;
    }
    const esp_err_t err = node_ctrl_apply_cmd(cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "command rejected: %s", esp_err_to_name(err)); /* e.g. lamp cmd outside manual mode */
        return;
    }
    node_status_t st;
    node_ctrl_get_status(&st);
    if (cmd->type == SL_CMD_CONFIG) {
        publish_config(&st); /* echo: the dashboard shows what the board really uses */
    }
    s_last_telemetry_ms = 0; /* push the new state on the next cycle */
}

static void drain_events(void)
{
    sl_mqtt_event_t ev;
    while (sl_mqtt_wait_event(&ev, 0)) {
        if (ev.type == SL_MQTT_EVT_CONNECTED) {
            ESP_LOGI(TAG, "MQTT connected, publishing config + history");
            node_status_t st;
            node_ctrl_get_status(&st);
            publish_config(&st); /* retained topics are re-sent after every reconnect */
            s_history_due = true;
            s_last_telemetry_ms = 0;
        } else if (ev.type == SL_MQTT_EVT_DISCONNECTED) {
            ESP_LOGW(TAG, "MQTT disconnected, esp-mqtt will reconnect");
        } else if (ev.type == SL_MQTT_EVT_COMMAND) {
            handle_command(&ev.cmd);
        }
    }
}

/* Start SNTP and MQTT once the router gave us an IP (one attempt; esp-mqtt reconnects itself) */
static void start_services(void)
{
    if (s_mqtt_started || !wifi_sta_is_connected()) {
        return;
    }
    (void)time_sync_start();
    s_mqtt_started = true;
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

void mqtt_link_step(void)
{
    if (!s_secrets_ok) {
        return;
    }
    start_services();
    if (!s_mqtt_started) {
        return;
    }
    drain_events();
    const int64_t t_ms = esp_timer_get_time() / 1000;
    if (sl_mqtt_is_connected() && t_ms - s_last_telemetry_ms >= (int64_t)APP_TELEMETRY_PERIOD_MS) {
        s_last_telemetry_ms = t_ms;
        node_status_t st;
        node_ctrl_get_status(&st);
        publish_telemetry(&st);
    }
}
