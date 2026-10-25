/**
 * @file tune_config.h
 * @brief WiFi access point settings for tune-by-wifi.
 *
 * Board wiring, timing and lighting constants stay in the shared
 * streetlight-node/main/app_config.h.
 */
#pragma once

#define TUNE_AP_SSID         "StreetLight-Tune"
#define TUNE_AP_PASSWORD     "12345678"   /* WPA2, at least 8 characters: change it before field use */
#define TUNE_AP_CHANNEL      6
#define TUNE_AP_MAX_CLIENTS  2            /* phone + laptop is enough, saves RAM */

/* Address of the board once connected: http://192.168.4.1 (ESP-IDF default AP netif) */
#define TUNE_AP_IP_TEXT      "192.168.4.1"

/* Max body of a POST command (JSON of a few fields) */
#define TUNE_HTTP_MAX_BODY   256U
