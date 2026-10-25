/**
 * @file dataset_naming.h
 * @brief Pure naming and validation rules of the dataset (no ESP-IDF calls, testable on a PC).
 *
 * File names follow Edge Impulse's convention: the label is the text before the first dot,
 * e.g. "nguoi.0007.pgm". Each capture session lives in its own folder "phien_001", so the
 * train/test split can be done per session (plans/01 section 5).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATASET_LABEL_COUNT      3U
#define DATASET_LABEL_MAX_LEN    8U   /* longest label + NUL */
#define DATASET_PERIOD_MIN_S     1U   /* SD write of one 460 KB frame takes a fraction of a second */
#define DATASET_PERIOD_MAX_S     3600U
#define DATASET_FRAME_NAME_CAP   32U
#define DATASET_SESSION_NAME_CAP 16U
#define DATASET_SEQ_MAX          999999U
#define DATASET_SESSION_MAX      9999U

/** Allowed labels: "nguoi" (person), "xe" (vehicle), "nen" (background only). */
extern const char *const DATASET_LABELS[DATASET_LABEL_COUNT];

bool dataset_label_valid(const char *label);

/** @return true when period_s is within DATASET_PERIOD_MIN_S..DATASET_PERIOD_MAX_S */
bool dataset_period_valid(uint32_t period_s);

/**
 * Build "<label>.<seq>.pgm".
 * @return length written, or -1 on a bad label/seq or when buf is too small
 */
int dataset_frame_name(char *buf, size_t cap, const char *label, uint32_t seq);

/** Build "phien_NNN". @return length written, or -1 on bad input */
int dataset_session_dir_name(char *buf, size_t cap, uint32_t session_no);

/** Parse a folder name "phien_NNN". @return true and fills *session_no for a valid name */
bool dataset_parse_session_dir(const char *name, uint32_t *session_no);

#ifdef __cplusplus
}
#endif
