/**
 * @file sl_secrets.c
 * @brief Read WiFi/MQTT credentials from NVS (see sl_secrets.h).
 */
#include "sl_secrets.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#define TAG          "sl_secrets"
#define NVS_NAMESPACE "secrets"

static esp_err_t read_str(nvs_handle_t h, const char *key, char *dst, size_t cap, bool required)
{
    size_t len = cap;
    const esp_err_t err = nvs_get_str(h, key, dst, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND && !required) {
        dst[0] = '\0';
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "key '%s': %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t sl_secrets_load(sl_secrets_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "namespace '%s' not found (%s), flash secrets first",
                 NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }
    err = read_str(h, "wifi_ssid", out->wifi_ssid, sizeof(out->wifi_ssid), true);
    if (err == ESP_OK) {
        err = read_str(h, "wifi_pass", out->wifi_pass, sizeof(out->wifi_pass), false);
    }
    if (err == ESP_OK) {
        err = read_str(h, "mqtt_uri", out->mqtt_uri, sizeof(out->mqtt_uri), true);
    }
    if (err == ESP_OK) {
        err = read_str(h, "mqtt_user", out->mqtt_user, sizeof(out->mqtt_user), true);
    }
    if (err == ESP_OK) {
        err = read_str(h, "mqtt_pass", out->mqtt_pass, sizeof(out->mqtt_pass), true);
    }
    nvs_close(h);
    return err;
}
