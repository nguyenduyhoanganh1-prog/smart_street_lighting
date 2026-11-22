/**
 * @file ai_pipeline.h
 * @brief Capture -> JPEG for the web stream -> FOMO detection -> lamp rule, in one task.
 *
 * Each cycle the camera frame is copied out and the camera is released at once, so the slow
 * parts (JPEG, inference ~ tens of ms) never hold it. The JPEG and the detections of the same
 * frame are published together, so the page can draw the boxes on exactly the picture they
 * belong to.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "detector.h"
#include "esp_err.h"
#include "road_zones.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t seq;               /**< increases by one per published frame, 0 = nothing yet */
    detection_result_t det;     /**< detections of that frame */
    uint32_t jpeg_bytes;
    uint32_t loop_ms;           /**< whole cycle: capture + JPEG + inference + publish */
    bool exposure_locked;
} ai_snapshot_t;

/** Allocate the frame buffers and start the task. */
esp_err_t ai_pipeline_start(void);

/**
 * Copy the latest JPEG into dst and the matching metadata into *meta.
 * @return JPEG length, or 0 when there is no frame yet or dst is too small
 */
size_t ai_pipeline_get_jpeg(uint8_t *dst, size_t cap, ai_snapshot_t *meta);

/** Metadata only (cheap). */
void ai_pipeline_get_snapshot(ai_snapshot_t *meta);

/** State of the start-up road calibration (see road_zones.h). */
typedef enum {
    AI_ROAD_PENDING = 0, /**< still calibrating (the first ~2 s after boot) */
    AI_ROAD_FOUND,       /**< zones follow the road */
    AI_ROAD_NOT_FOUND,   /**< fallback: zones are the 4 quarters of the frame width */
} ai_road_state_t;

/** Copy the calibration result into *road (meaningful only when the state is AI_ROAD_FOUND). */
ai_road_state_t ai_pipeline_get_road(road_zones_t *road);

/**
 * Find the road again (the page's "Tìm lại đường" button). The state turns AI_ROAD_PENDING at once;
 * the pipeline task runs the calibration before its next frame (every lamp at full level ~2 s), then
 * puts the lamps back as they were (levels and automatic mode). Safe to call from any task.
 */
void ai_pipeline_request_recalibration(void);

/** Freeze / release auto exposure + gain so brightness stops following the lamps. */
esp_err_t ai_pipeline_set_exposure_lock(bool lock);

#ifdef __cplusplus
}
#endif
