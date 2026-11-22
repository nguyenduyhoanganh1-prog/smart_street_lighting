/**
 * @file detector.h
 * @brief FOMO object detection (Edge Impulse export) behind a small C interface.
 *
 * Input: one 8-bit gray frame (any size, here 320x240). It is squashed to the model input
 * (128x128, the same resize mode Studio used for training) and run through the int8 model.
 * Output: object centres normalised to the frame (0..1), so the caller needs no model details.
 * Not thread-safe: call detector_run() from one task only.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DETECTOR_MAX_OBJECTS 10U

typedef struct {
    uint8_t label;  /**< index for detector_label_name() */
    float score;    /**< 0..1 */
    float cx;       /**< centre x, 0..1 of the frame width */
    float cy;       /**< centre y, 0..1 of the frame height */
    float w;        /**< box width, 0..1 (FOMO boxes are whole grid cells) */
    float h;        /**< box height, 0..1 */
} detection_t;

typedef struct {
    uint32_t count;
    detection_t items[DETECTOR_MAX_OBJECTS];
    uint32_t resize_us;  /**< frame -> model input */
    uint32_t dsp_us;     /**< Edge Impulse image block (gray -> int8) */
    uint32_t infer_us;   /**< neural network */
} detection_result_t;

/** Run one warm-up inference and log heap use (arena in SRAM or PSRAM), timing and model facts. */
esp_err_t detector_init(void);

/** @return ESP_OK, ESP_ERR_INVALID_ARG (bad pointer/size) or ESP_FAIL (resize or inference error) */
esp_err_t detector_run(const uint8_t *gray, uint32_t width, uint32_t height, detection_result_t *out);

/** Class name of a detection ("car", "human"); "?" for an unknown index. */
const char *detector_label_name(uint8_t label);

#ifdef __cplusplus
}
#endif
