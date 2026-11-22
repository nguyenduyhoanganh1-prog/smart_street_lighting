/**
 * @file detector.cpp
 * @brief FOMO detection with the Edge Impulse C++ library (see detector.h).
 *
 * Preprocessing must match training: the dataset was captured as 320x240 8-bit gray PNG and
 * uploaded to Studio, which squashed it to 128x128. Here the same squash is done with the SDK's
 * own resize (resize_image_using_mode) and the gray byte is packed as 0xGGGGGG, the RGB layout
 * the image block expects; its gray conversion (luma weights sum to 1) gives back the same byte.
 */
#include "detector.h"

#include <string.h>

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/image/processing.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "detector"

/* A re-exported model that changes any of these must not build silently */
static_assert(EI_CLASSIFIER_INPUT_WIDTH == 128 && EI_CLASSIFIER_INPUT_HEIGHT == 128,
              "model input is no longer 128x128: update docs and check the frame -> input resize");
static_assert(EI_CLASSIFIER_NN_INPUT_FRAME_SIZE == EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT,
              "model input is not single-channel (gray)");
static_assert(EI_CLASSIFIER_RESIZE_MODE == EI_CLASSIFIER_RESIZE_SQUASH, "resize mode changed");
static_assert(EI_CLASSIFIER_OBJECT_DETECTION == 1, "not an object detection model");
static_assert(EI_CLASSIFIER_QUANTIZATION_ENABLED == 1, "model is not int8 quantized");

namespace {

constexpr size_t kInputPixels = static_cast<size_t>(EI_CLASSIFIER_INPUT_WIDTH) * EI_CLASSIFIER_INPUT_HEIGHT;
constexpr uint32_t kWarmupGray = 128U;

uint8_t s_input[kInputPixels]; /* resized gray frame, 16 KB static */

/** Signal callback: hand the SDK `length` pixels starting at `offset`, packed as 0xGGGGGG. */
int get_input_data(size_t offset, size_t length, float *out_ptr)
{
    if (offset + length > kInputPixels) {
        return -1;
    }
    for (size_t i = 0; i < length; i++) {
        const uint32_t g = s_input[offset + i];
        out_ptr[i] = static_cast<float>((g << 16) | (g << 8) | g); /* <= 0xFFFFFF: exact in a float */
    }
    return 0;
}

int label_index(const char *name)
{
    for (int k = 0; k < EI_CLASSIFIER_LABEL_COUNT; k++) {
        if (strcmp(name, ei_default_impulse.impulse->categories[k]) == 0) {
            return k;
        }
    }
    return -1;
}

void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %-22s internal free %7u (min %7u)  PSRAM free %7u (min %7u)", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

} // namespace

/*
 * Replaces the weak ei_calloc() of edge-impulse-sdk/porting/espressif. The compiled model asks
 * it for ESP-NN scratch buffers that do not fit in its arena, and the ESP32-S3 ESP-NN assembly
 * needs them 16-byte aligned: with the stock heap_caps_calloc() the vector stores landed before
 * the buffer and corrupted the heap (crash in free() right after the first inference).
 * Memory from heap_caps_aligned_calloc() is released by the SDK's ei_free() -> free() as usual.
 */
void *ei_calloc(size_t nitems, size_t size)
{
    constexpr size_t kAlignBytes = 16U;
    return heap_caps_aligned_calloc(kAlignBytes, nitems, size, MALLOC_CAP_DEFAULT);
}

esp_err_t detector_init(void)
{
    ESP_LOGI(TAG, "model: %ux%u input, %d classes, FOMO int8, TFLite arena %u bytes (allocated per inference)",
             (unsigned)EI_CLASSIFIER_INPUT_WIDTH, (unsigned)EI_CLASSIFIER_INPUT_HEIGHT,
             (int)EI_CLASSIFIER_LABEL_COUNT, (unsigned)EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE);
    for (int k = 0; k < EI_CLASSIFIER_LABEL_COUNT; k++) {
        ESP_LOGI(TAG, "  class %d = %s", k, ei_default_impulse.impulse->categories[k]);
    }
    log_heap("before first inference");

    uint8_t warmup[64 * 48];
    memset(warmup, kWarmupGray, sizeof(warmup));
    detection_result_t result;
    const esp_err_t err = detector_run(warmup, 64U, 48U, &result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "warm-up inference failed");
        return err;
    }
    ESP_LOGI(TAG, "warm-up: resize %u us, dsp %u us, inference %u us", (unsigned)result.resize_us,
             (unsigned)result.dsp_us, (unsigned)result.infer_us);
    log_heap("after first inference");
    return ESP_OK;
}

esp_err_t detector_run(const uint8_t *gray, uint32_t width, uint32_t height, detection_result_t *out)
{
    if (gray == NULL || out == NULL || width == 0U || height == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    const int64_t t0 = esp_timer_get_time();
    const int rc = ei::image::processing::resize_image_using_mode(
        gray, static_cast<int>(width), static_cast<int>(height), s_input, EI_CLASSIFIER_INPUT_WIDTH,
        EI_CLASSIFIER_INPUT_HEIGHT, 1, EI_CLASSIFIER_RESIZE_MODE);
    if (rc != 0) {
        ESP_LOGE(TAG, "resize failed (%d)", rc);
        return ESP_FAIL;
    }
    out->resize_us = static_cast<uint32_t>(esp_timer_get_time() - t0);

    ei::signal_t signal;
    signal.total_length = kInputPixels;
    signal.get_data = &get_input_data;

    ei_impulse_result_t result = {};
    const EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    if (err != EI_IMPULSE_OK) {
        ESP_LOGE(TAG, "run_classifier failed (%d)", static_cast<int>(err));
        return ESP_FAIL;
    }
    out->dsp_us = static_cast<uint32_t>(result.timing.dsp_us);
    out->infer_us = static_cast<uint32_t>(result.timing.classification_us);

    for (uint32_t i = 0; i < result.bounding_boxes_count && out->count < DETECTOR_MAX_OBJECTS; i++) {
        const ei_impulse_result_bounding_box_t &bb = result.bounding_boxes[i];
        const int label = (bb.value > 0.0f && bb.label != NULL) ? label_index(bb.label) : -1;
        if (label < 0) {
            continue; /* empty slot or an unknown class */
        }
        detection_t &d = out->items[out->count++];
        d.label = static_cast<uint8_t>(label);
        d.score = bb.value;
        d.w = static_cast<float>(bb.width) / EI_CLASSIFIER_INPUT_WIDTH;
        d.h = static_cast<float>(bb.height) / EI_CLASSIFIER_INPUT_HEIGHT;
        d.cx = (static_cast<float>(bb.x) + 0.5f * bb.width) / EI_CLASSIFIER_INPUT_WIDTH;
        d.cy = (static_cast<float>(bb.y) + 0.5f * bb.height) / EI_CLASSIFIER_INPUT_HEIGHT;
    }
    return ESP_OK;
}

const char *detector_label_name(uint8_t label)
{
    return label < EI_CLASSIFIER_LABEL_COUNT ? ei_default_impulse.impulse->categories[label] : "?";
}
