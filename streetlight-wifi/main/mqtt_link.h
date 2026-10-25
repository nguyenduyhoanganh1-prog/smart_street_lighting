/**
 * @file mqtt_link.h
 * @brief Glue between node_ctrl and the MQTT client: publishes state/energy/config/history
 *        and applies dashboard commands. Does nothing if no credentials are stored in NVS.
 *
 * Call order: nvs_flash_init() (done by node_ctrl_init) -> mqtt_link_init() ->
 * wifi_ap_start(mqtt_link_wifi_ssid(), mqtt_link_wifi_pass()) -> mqtt_link_step() every cycle.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load WiFi + MQTT credentials from NVS. @return false if missing (AP-only mode, no MQTT). */
bool mqtt_link_init(void);

/** Router SSID/password loaded by mqtt_link_init(); "" when init failed. */
const char *mqtt_link_wifi_ssid(void);
const char *mqtt_link_wifi_pass(void);

/**
 * Non-blocking step, call every control cycle: starts SNTP + MQTT once the router link
 * is up, handles dashboard commands and publishes telemetry every APP_TELEMETRY_PERIOD_MS.
 */
void mqtt_link_step(void);

#ifdef __cplusplus
}
#endif
