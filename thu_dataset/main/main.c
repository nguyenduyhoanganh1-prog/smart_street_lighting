/**
 * @file main.c
 * @brief thu_dataset: capture 640x360 grayscale frames to an SD card from a WiFi web page.
 *
 *   1. Join WiFi "StreetLight-Dataset" (see tune_config.h)
 *   2. Open http://192.168.4.1
 *   3. Pick a label, press "Chụp 1 ảnh" or start the periodic capture.
 *
 * Frames land in /sdcard/dataset/phien_NNN/ (one folder per capture session).
 */
#include "camera_cap.h"
#include "capture_ctrl.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sd_store.h"
#include "web_server.h"
#include "wifi_ap.h"

#define TAG "thu_dataset"

void app_main(void)
{
    ESP_LOGI(TAG, "thu_dataset starting");

    /* The WiFi driver keeps calibration data in NVS. Never erase it automatically. */
    const esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(nvs_err));
    }

    /* A failing camera or card must not stop the web page: it reports the problem to the user */
    const bool camera_ok = (camera_cap_init() == ESP_OK);
    const bool sd_ok = (sd_store_mount() == ESP_OK);
    (void)capture_ctrl_init(camera_ok, sd_ok);

    esp_err_t err = wifi_ap_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_ap_start failed: %s", esp_err_to_name(err));
        return;
    }
    err = web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web_server_start failed: %s", esp_err_to_name(err));
    }
    /* Nothing else to do here: the HTTP server and the auto-capture task keep running */
}
