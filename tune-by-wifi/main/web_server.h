/**
 * @file web_server.h
 * @brief HTTP server: serves the tuning page and a small JSON API.
 *
 *   GET  /                    the web page (embedded index.html)
 *   GET  /api/status          live values + config as JSON
 *   POST /api/cmd/<name>      name = mode | lamp | force_night | config,
 *                             body = same JSON as the MQTT command (see sl_mqtt_codec.h)
 *   POST /api/time            body = Unix seconds (UTC) from the browser clock
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif
