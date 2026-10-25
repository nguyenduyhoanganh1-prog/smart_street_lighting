/**
 * @file sl_secrets.h
 * @brief WiFi and MQTT credentials stored in NVS (never in source code).
 *
 * NVS namespace "secrets", string keys:
 *   wifi_ssid, wifi_pass (optional), mqtt_uri, mqtt_user, mqtt_pass
 * Flash them from a CSV, see secrets.example.csv in this component.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];  /* max 32 chars */
    char wifi_pass[65];  /* max 64 chars, "" = open network */
    char mqtt_uri[128];
    char mqtt_user[64];
    char mqtt_pass[64];
} sl_secrets_t;

/**
 * Load all credentials. Requires nvs_flash_init() first.
 * Declare *out as static: the struct is ~350 bytes, too big for a small task stack.
 * @return ESP_ERR_NVS_NOT_FOUND if a required key is missing (name is logged)
 */
esp_err_t sl_secrets_load(sl_secrets_t *out);

#ifdef __cplusplus
}
#endif
