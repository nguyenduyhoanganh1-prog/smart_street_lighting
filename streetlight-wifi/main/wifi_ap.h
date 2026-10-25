/**
 * @file wifi_ap.h
 * @brief WiFi: the board always creates its own access point (tuning page) and, when
 *        router credentials are given, also joins that router (AP+STA) for MQTT/SNTP.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the AP (SSID/password from tune_config.h). Needs nvs_flash_init() done first.
 * @param sta_ssid  router SSID, or NULL/"" for AP only (no Internet, no MQTT)
 * @param sta_pass  router password ("" = open network); ignored when sta_ssid is empty
 */
esp_err_t wifi_ap_start(const char *sta_ssid, const char *sta_pass);

/** true while the station side has an IP address from the router. */
bool wifi_sta_is_connected(void);

#ifdef __cplusplus
}
#endif
