/**
 * @file sl_wifi.h
 * @brief WiFi station with automatic reconnect (ESP32 supports 2.4 GHz only).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Init netif/event loop/WiFi and start connecting. Requires nvs_flash_init()
 * first (the WiFi driver stores calibration data in NVS).
 * @param pass  empty string for an open network
 */
esp_err_t sl_wifi_start(const char *ssid, const char *pass);

/** Block until an IP address is obtained. @return false on timeout */
bool sl_wifi_wait_connected(uint32_t timeout_ms);

bool sl_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
