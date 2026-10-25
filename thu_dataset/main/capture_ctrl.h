/**
 * @file capture_ctrl.h
 * @brief Capture logic: manual shot, periodic auto-capture, session, label and lamp levels.
 *
 * Threading: the web server (httpd task) and the auto-capture task both call in here.
 * One "operation" mutex serialises the long actions (grab + SD write, new session); a short
 * "state" mutex protects the status copy. Order is always operation -> camera -> state.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "dataset_naming.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAPTURE_ERROR_CAP 96U

/** Copy of everything the web page shows. Plain data, safe to use without any lock. */
typedef struct {
    char label[DATASET_LABEL_MAX_LEN];
    bool auto_on;
    uint32_t period_s;
    uint32_t session_no;          /* 0 = no session yet (created by the first capture) */
    uint32_t session_count;       /* frames saved in this session */
    uint32_t total_count;         /* frames saved since boot */
    bool cam_ok;
    bool sd_ok;
    bool time_valid;
    bool exposure_locked;
    uint8_t lamp_pct[APP_LAMP_COUNT];
    uint64_t sd_total_bytes;
    uint64_t sd_free_bytes;
    char last_file[DATASET_FRAME_NAME_CAP];
    char error[CAPTURE_ERROR_CAP]; /* "" when everything is fine, otherwise a message for the user */
} capture_status_t;

/**
 * Init lamps and the auto-capture task. Call after camera_cap_init() and sd_store_mount();
 * pass whether each of them succeeded.
 */
esp_err_t capture_ctrl_init(bool camera_ok, bool sd_ok);

void capture_ctrl_get_status(capture_status_t *out);

/** Take one frame now with the current label. */
esp_err_t capture_ctrl_capture_now(void);

/**
 * Start or stop periodic capture.
 * @return ESP_ERR_INVALID_ARG for a period outside DATASET_PERIOD_MIN_S..MAX_S,
 *         ESP_ERR_INVALID_STATE when the camera or SD card is not working
 */
esp_err_t capture_ctrl_set_auto(bool on, uint32_t period_s);

/** @return ESP_ERR_INVALID_ARG when the label is not one of DATASET_LABELS */
esp_err_t capture_ctrl_set_label(const char *label);

/** Start a new session folder; the frame counter of the session restarts at 0. */
esp_err_t capture_ctrl_new_session(void);

/** Set a lamp level 0..100 %. lamp = 0 sets all four lamps, 1..4 one lamp. */
esp_err_t capture_ctrl_set_lamp(unsigned lamp, uint8_t pct);

esp_err_t capture_ctrl_set_exposure_lock(bool lock);

/** Set the system clock from the browser (Unix seconds, UTC). Ignored if implausible. */
esp_err_t capture_ctrl_set_time(int64_t epoch_s);

#ifdef __cplusplus
}
#endif
