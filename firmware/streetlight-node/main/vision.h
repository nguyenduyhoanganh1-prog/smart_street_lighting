/**
 * @file vision.h
 * @brief Camera + FOMO + road calibration in their own task, away from the 200 ms control loop.
 *
 * Why a separate task: one inference takes 200-300 ms (plans/01 target 3-5 per second), which alone
 * would eat the whole 200 ms control cycle. The vision task runs as fast as FOMO allows and drops its
 * latest result into a mailbox; the control loop reads that mailbox without ever waiting.
 *
 * Night only: by day the lamps are off and nothing needs detecting, so the task sleeps and only
 * checks the camera every APP_CAMERA_CHECK_PERIOD_MS (a broken camera is then known before dark).
 *
 * Road calibration (road_zones.h) runs after EVERY start (day or night, once the control task runs),
 * then at the start of each night until it succeeds (at most APP_CALIB_MAX_ATTEMPTS per try), or when
 * vision_request_recalibration() is called. While it runs vision_calibrating() is true and the control
 * loop must hold every lamp at full level (so the lamps flash on for a few seconds after a daytime reset).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "detector.h"
#include "esp_err.h"
#include "road_zones.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VISION_ROAD_PENDING = 0, /**< not tried yet this night (or a new calibration was requested) */
    VISION_ROAD_FOUND,       /**< road + zones valid */
    VISION_ROAD_NOT_FOUND,   /**< every attempt failed: zones fall back to the quarters of the frame */
} vision_road_state_t;

typedef struct {
    bool camera_ok;                 /**< camera + model work (false: init failed or frames stopped) */
    int64_t t_ms;                   /**< esp_timer ms of the last result, 0 = none yet */
    int64_t frame_ms;               /**< esp_timer ms when that result's frame was taken from the camera */
    detection_result_t det;         /**< detections of that frame */
    float fps;                      /**< inferences per second over the last window */
    vision_road_state_t road_state;
    road_zones_t road;              /**< meaningful when road_state == VISION_ROAD_FOUND */
} vision_snapshot_t;

/**
 * Init the camera and the model, then start the task. Call before WiFi (the model arena and the frame
 * buffers want PSRAM first). On failure the snapshot says camera_ok = false and no task is started:
 * the caller keeps running in the time-based lighting mode.
 */
esp_err_t vision_start(void);

/** Tell the task whether it is night (called by the control loop every cycle). */
void vision_set_night(bool night);

/** true while the road calibration needs every lamp at full level. */
bool vision_calibrating(void);

/** Run the road calibration again (the next night cycle of the task starts it). */
void vision_request_recalibration(void);

/** Copy of the latest result. Never blocks for long. */
void vision_get(vision_snapshot_t *out);

/** Last road calibration attempt, for the dashboard (the frame as JPEG + what was found in it). */
typedef struct {
    uint32_t seq;        /**< +1 per attempt, 0 = none yet */
    int64_t t_ms;        /**< esp_timer ms of the attempt */
    bool found;
    road_zones_t road;   /**< meaningful when found */
    size_t jpeg_len;     /**< 0 = no picture (no camera frame or JPEG too large) */
} vision_calib_info_t;

/** seq of the last calibration attempt (cheap: compare with what was already sent). */
uint32_t vision_calib_seq(void);

/**
 * Copy the last calibration attempt: its JPEG into jpeg (cap >= APP_CALIB_JPEG_BYTES) and the rest
 * into *info. Uses its own lock, so it never delays vision_get(). @return false if nothing to copy
 */
bool vision_get_calib(uint8_t *jpeg, size_t cap, vision_calib_info_t *info);

/**
 * Ask for a JPEG of the next analysed frame (stopped-object alert). id != 0 tags the picture; a newer
 * request replaces an older one that was not taken yet.
 */
void vision_request_alert_image(uint32_t id);

/**
 * Copy the alert picture tagged id into jpeg (cap >= APP_CALIB_JPEG_BYTES), and the detections of that
 * same frame into *det (may be NULL).
 * @return false while that picture is not ready yet; true once it is (then *len = 0 if encoding failed)
 */
bool vision_get_alert_image(uint32_t id, uint8_t *jpeg, size_t cap, size_t *len, detection_result_t *det);

/** Only the detections of the alert picture tagged id. @return false while that picture is not taken yet */
bool vision_get_alert_det(uint32_t id, detection_result_t *det);

#ifdef __cplusplus
}
#endif
