/**
 * @file time_sync.h
 * @brief SNTP time sync and Vietnam time zone (UTC+7).
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Set TZ to UTC+7 so localtime_r() returns Vietnam time. Call once at boot. */
void time_sync_set_timezone(void);

/** Start SNTP (once; later calls are no-ops). Call after WiFi has an IP. */
esp_err_t time_sync_start(void);

/** true once the system clock holds a real date (SNTP has synced). */
bool time_sync_is_valid(void);

#ifdef __cplusplus
}
#endif
