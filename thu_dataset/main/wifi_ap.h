/**
 * @file wifi_ap.h
 * @brief WiFi access point: the board creates its own network, no router needed.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the AP (SSID/password from tune_config.h). Needs nvs_flash_init() done first. */
esp_err_t wifi_ap_start(void);

#ifdef __cplusplus
}
#endif
