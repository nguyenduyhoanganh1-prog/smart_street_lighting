/**
 * @file camera_cap.h
 * @brief OV2640 camera in 8-bit grayscale, one owner at a time.
 *
 * The sensor runs at VGA (640x480). camera_cap_crop() returns a view of the middle 360 rows,
 * i.e. the 640x360 frame that is stored on the SD card and shown on the web page.
 * A mutex makes sure the capture task and the web preview never use the camera together.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_camera.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Init the driver (grayscale, VGA, 2 frame buffers in PSRAM) and check the frame size. */
esp_err_t camera_cap_init(void);

/**
 * Take the camera and grab the latest frame. Every non-NULL result MUST be given back with
 * camera_cap_release(), which also lets the next user in.
 * @return NULL on timeout, camera error or an unexpected frame size (mutex already released)
 */
camera_fb_t *camera_cap_acquire(uint32_t timeout_ms);

/** Return the frame buffer and release the camera. */
void camera_cap_release(camera_fb_t *fb);

/** Fill *out with a 640x360 view (middle rows) of a frame from camera_cap_acquire(). No copy. */
void camera_cap_crop(const camera_fb_t *fb, camera_fb_t *out);

/**
 * Freeze (lock = true) or release (lock = false) auto exposure and auto gain. Locking keeps the
 * exposure the sensor had at that moment, so brightness no longer changes with the lamp level.
 */
esp_err_t camera_cap_set_exposure_lock(bool lock);

#ifdef __cplusplus
}
#endif
