/**
 * @file web_server.h
 * @brief HTTP server: control page, status JSON, JPEG preview and command routes.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the server on port 80 (the WiFi AP must be up). */
esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif
