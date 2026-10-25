/**
 * @file node_ctrl.h
 * @brief Control core of tune-by-wifi: sensors -> lighting rules -> PWM -> energy.
 *
 * Same behaviour as streetlight-node/main/main.c without MQTT. One mutex protects
 * the state, so the HTTP task can read a status snapshot and apply commands while
 * node_ctrl_step() runs in the main task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"
#include "sl_mqtt_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Copy of everything the web page shows. Plain data, safe to use without the lock. */
typedef struct {
    /* Lighting state */
    bool night;
    bool force_night;
    sl_mode_t mode;                     /* auto / manual / schedule */
    uint32_t manual_left_s;             /* seconds until manual mode returns to auto, 0 if not manual */
    uint8_t duty[APP_LAMP_COUNT];       /* current output, % */

    /* Sensors */
    bool lux_ok;
    float lux;
    bool ina_ok[APP_LAMP_COUNT];
    float v_volt[APP_LAMP_COUNT];
    float i_ma[APP_LAMP_COUNT];
    float p_mw[APP_LAMP_COUNT];

    /* Energy (mWh) and saving % (NAN = unknown) */
    double e_today_mwh[APP_LAMP_COUNT];
    double e_total_mwh[APP_LAMP_COUNT];
    float saving_pct[APP_LAMP_COUNT];

    /* Clock (set from the browser, no SNTP) */
    bool time_valid;
    uint16_t minute_of_day;             /* local, valid if time_valid */

    uint32_t uptime_s;
    sl_config_t config;                 /* values the board really uses */
} node_status_t;

/** Init PWM, sensors, NVS (config + energy). Never erases NVS. */
esp_err_t node_ctrl_init(void);

/** One control cycle. Call every APP_SAMPLE_PERIOD_MS. */
void node_ctrl_step(void);

/** Snapshot for the web page. */
void node_ctrl_get_status(node_status_t *out);

/**
 * Apply a command decoded by sl_codec_decode_cmd() (mode, lamp, force_night, config).
 * @return ESP_OK, ESP_ERR_INVALID_STATE (lamp command outside manual mode),
 *         ESP_ERR_NOT_SUPPORTED (other command types)
 */
esp_err_t node_ctrl_apply_cmd(const sl_cmd_t *cmd);

/** Set the system clock from the browser (Unix seconds, UTC). Ignored if implausible. */
esp_err_t node_ctrl_set_time(int64_t epoch_s);

#ifdef __cplusplus
}
#endif
