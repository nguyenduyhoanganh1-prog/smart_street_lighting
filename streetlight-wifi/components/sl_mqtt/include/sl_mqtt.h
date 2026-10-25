/**
 * @file sl_mqtt.h
 * @brief MQTT/TLS client for the streetlight node (HiveMQ Cloud, esp-mqtt).
 *
 * Topics (prefix "streetlight/"), see dashboard/src/lib/protocol.ts:
 *   publish   online (LWT), state, energy, energy/history, config, alert, alert/image
 *   subscribe cmd/#  -> delivered to the app through sl_mqtt_wait_event()
 *
 * All publish functions are thread-safe and return ESP_ERR_INVALID_STATE while
 * the broker is not connected. They send synchronously over TLS, so call them
 * from a communication/low-priority task, never from the lamp control loop or an ISR.
 * Retained topics should be re-published by the app on SL_MQTT_EVT_CONNECTED.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sl_mqtt_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SL_MQTT_TOPIC_PREFIX   "streetlight"
#define SL_MQTT_IMAGE_MAX_JPEG (30U * 1024U) /* bigger JPEGs are rejected */
#define SL_MQTT_WAIT_FOREVER   UINT32_MAX

typedef struct {
    const char *uri;       /* e.g. "mqtts://xxxx.s1.eu.hivemq.cloud:8883" */
    const char *username;  /* HiveMQ Cloud access credential */
    const char *password;
    const char *client_id; /* NULL -> esp-mqtt default (derived from MAC) */
} sl_mqtt_config_t;

typedef enum {
    SL_MQTT_EVT_CONNECTED = 0, /* re-publish config/history/alert here */
    SL_MQTT_EVT_DISCONNECTED,
    SL_MQTT_EVT_COMMAND,       /* cmd field is valid */
} sl_mqtt_evt_type_t;

typedef struct {
    sl_mqtt_evt_type_t type;
    sl_cmd_t cmd;
} sl_mqtt_event_t;

/**
 * Start the client (call once, after WiFi is up or while it connects).
 * esp-mqtt copies the strings, so cfg may be freed afterwards.
 */
esp_err_t sl_mqtt_start(const sl_mqtt_config_t *cfg);

bool sl_mqtt_is_connected(void);

/**
 * Wait for the next connection event or dashboard command.
 * @param timeout_ms  SL_MQTT_WAIT_FOREVER to block, 0 to poll
 * @return true when *out was filled
 */
bool sl_mqtt_wait_event(sl_mqtt_event_t *out, uint32_t timeout_ms);

esp_err_t sl_mqtt_publish_state(const sl_state_t *state);
esp_err_t sl_mqtt_publish_energy(const sl_lamp_energy_t lamps[SL_LAMP_COUNT]);
esp_err_t sl_mqtt_publish_history(int64_t t_sec,
                                  const uint32_t mwh[SL_LAMP_COUNT][SL_HISTORY_HOURS]);
esp_err_t sl_mqtt_publish_config(const sl_config_t *cfg);
esp_err_t sl_mqtt_publish_alert(const sl_alert_t *alert);
/** Close the alert: clears the retained alert and alert/image topics */
esp_err_t sl_mqtt_clear_alert(void);
/** Publish a JPEG snapshot (base64) to alert/image, retained */
esp_err_t sl_mqtt_publish_image(const uint8_t *jpeg, size_t len);

#ifdef __cplusplus
}
#endif
