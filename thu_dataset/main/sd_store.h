/**
 * @file sd_store.h
 * @brief SD card storage of the dataset (1-bit SDMMC, FAT).
 *
 * Layout:
 *   /sdcard/dataset/phien_001/nguoi.0001.pgm      frames, 640x360 8-bit grayscale (binary PGM, P5)
 *   /sdcard/dataset/phien_001/session.json        written once when the session starts
 *   /sdcard/dataset/phien_001/index.csv           one line per frame (label, lamp levels, ...)
 *
 * Not thread-safe: the caller (capture_ctrl) serialises all calls, so the module can keep one
 * static scratch buffer for paths instead of using the stack or the heap.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Facts stored next to every frame in index.csv. */
typedef struct {
    uint8_t lamp_pct[APP_LAMP_COUNT];
    bool exposure_locked;
    int64_t epoch_s;            /* Unix seconds, 0 when the clock was not set */
} sd_frame_meta_t;

/** Mount the card. Never formats it. Insert the card BEFORE power-up: no hot-plug. */
esp_err_t sd_store_mount(void);

bool sd_store_is_mounted(void);

/** Card capacity and free space in bytes. */
esp_err_t sd_store_space(uint64_t *total_bytes, uint64_t *free_bytes);

/**
 * Create the next session folder (highest existing number + 1) with session.json and the
 * index.csv header. The number comes from the folders on the card, not from a clock.
 */
esp_err_t sd_store_begin_session(uint32_t *session_no, int64_t epoch_s, bool exposure_locked);

/**
 * Write one grayscale frame as a PGM file (small header, then the pixels) and append its line to
 * index.csv. A partly written frame file is removed again.
 * @param len       must be APP_FRAME_BYTES (640 x 360 pixels, one byte each)
 * @param name_out  receives the file name, e.g. "nguoi.0007.pgm"
 */
esp_err_t sd_store_save_frame(uint32_t session_no, const char *label, uint32_t seq,
                              const uint8_t *data, size_t len, const sd_frame_meta_t *meta,
                              char *name_out, size_t name_cap);

#ifdef __cplusplus
}
#endif
