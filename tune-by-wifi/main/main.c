/**
 * @file main.c
 * @brief tune-by-wifi: the ESP32 opens a WiFi access point and serves a web page to
 *        watch the sensors and tune the lighting parameters from a phone or laptop.
 *
 *   1. Join WiFi "StreetLight-Tune" (see tune_config.h)
 *   2. Open http://192.168.4.1
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_ctrl.h"
#include "web_server.h"
#include "wifi_ap.h"

#define TAG "tune"

static const char *const MODE_TEXT[] = { "auto", "manual", "schedule" };

/* Same style as the serial log of streetlight-node: one line per lamp */
static void log_status(void)
{
    node_status_t s;
    node_ctrl_get_status(&s);
    ESP_LOGI(TAG, "mode=%s | %s%s | lux=%s", MODE_TEXT[s.mode], s.night ? "NIGHT" : "DAY",
             s.force_night ? " (forced)" : "", s.lux_ok ? "ok" : "BH1750 offline");
    if (s.lux_ok) {
        ESP_LOGI(TAG, "BH1750     lux=%.1f", (double)s.lux);
    }
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        if (!s.ina_ok[k]) {
            ESP_LOGW(TAG, "Lamp %u  duty=%3u%%  INA219 offline", k + 1U, (unsigned)s.duty[k]);
            continue;
        }
        ESP_LOGI(TAG, "Lamp %u  duty=%3u%%  V=%.3f V  I=%6.2f mA  P=%6.1f mW", k + 1U, (unsigned)s.duty[k],
                 (double)s.v_volt[k], (double)s.i_ma[k], (double)s.p_mw[k]);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "tune-by-wifi starting");
    (void)node_ctrl_init(); /* logs its own errors; the lamps still work without NVS */

    esp_err_t err = wifi_ap_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_ap_start failed: %s", esp_err_to_name(err));
    } else {
        err = web_server_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "web_server_start failed: %s", esp_err_to_name(err));
        }
    }

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t cycles_since_log = 0;
    const uint32_t log_every = APP_LOG_PERIOD_MS / APP_SAMPLE_PERIOD_MS;

    for (;;) {
        node_ctrl_step();
        if (++cycles_since_log >= log_every) {
            cycles_since_log = 0;
            log_status();
        }
        (void)xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}
