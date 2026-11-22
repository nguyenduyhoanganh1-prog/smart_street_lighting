/**
 * @file main.c
 * @brief ai_stream: live camera stream with on-board FOMO detection and lamp control.
 *
 *   1. Join WiFi "StreetLight-AI" (see tune_config.h)
 *   2. Open http://192.168.4.1
 *   3. Watch the boxes the model draws, move the 4 lamp sliders or let the lamps follow the model.
 *
 * Start-up order matters for memory: camera first (its frame buffers go to PSRAM), then lamps,
 * then the model (first inference allocates the TFLite arena), then WiFi + web.
 */
#include "ai_pipeline.h"
#include "camera_cap.h"
#include "detector.h"
#include "esp_log.h"
#include "lamp_ctrl.h"
#include "nvs_flash.h"
#include "web_server.h"
#include "wifi_ap.h"

#define TAG "ai_stream"

void app_main(void)
{
    ESP_LOGI(TAG, "ai_stream starting");

    /* The WiFi driver keeps calibration data in NVS. Never erase it automatically. */
    const esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(nvs_err));
    }

    esp_err_t err = camera_cap_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed (%s): no stream, check the camera wiring", esp_err_to_name(err));
        return;
    }
    (void)lamp_ctrl_init(); /* a lamp problem is logged; the stream still works */

    err = detector_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "detector init failed (%s)", esp_err_to_name(err));
        return;
    }
    err = ai_pipeline_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ai_pipeline_start failed (%s)", esp_err_to_name(err));
        return;
    }

    err = wifi_ap_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_ap_start failed: %s", esp_err_to_name(err));
        return;
    }
    err = web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web_server_start failed: %s", esp_err_to_name(err));
    }
    /* Nothing else to do here: the pipeline task and the HTTP server keep running */
}
