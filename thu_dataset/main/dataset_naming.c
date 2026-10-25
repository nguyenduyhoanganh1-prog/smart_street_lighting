/**
 * @file dataset_naming.c
 * @brief Pure naming and validation rules of the dataset (see dataset_naming.h).
 */
#include "dataset_naming.h"

#include <stdio.h>
#include <string.h>

#define SESSION_PREFIX     "phien_"
#define SESSION_MAX_DIGITS 4U

const char *const DATASET_LABELS[DATASET_LABEL_COUNT] = { "nguoi", "xe", "nen" };

bool dataset_label_valid(const char *label)
{
    if (label == NULL) {
        return false;
    }
    for (size_t i = 0; i < DATASET_LABEL_COUNT; i++) {
        if (strcmp(label, DATASET_LABELS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool dataset_period_valid(uint32_t period_s)
{
    return period_s >= DATASET_PERIOD_MIN_S && period_s <= DATASET_PERIOD_MAX_S;
}

int dataset_frame_name(char *buf, size_t cap, const char *label, uint32_t seq)
{
    if (buf == NULL || !dataset_label_valid(label) || seq == 0U || seq > DATASET_SEQ_MAX) {
        return -1;
    }
    const int n = snprintf(buf, cap, "%s.%04u.pgm", label, (unsigned)seq);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

int dataset_session_dir_name(char *buf, size_t cap, uint32_t session_no)
{
    if (buf == NULL || session_no == 0U || session_no > DATASET_SESSION_MAX) {
        return -1;
    }
    const int n = snprintf(buf, cap, SESSION_PREFIX "%03u", (unsigned)session_no);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

bool dataset_parse_session_dir(const char *name, uint32_t *session_no)
{
    const size_t prefix_len = sizeof(SESSION_PREFIX) - 1U;
    if (name == NULL || session_no == NULL || strncmp(name, SESSION_PREFIX, prefix_len) != 0) {
        return false;
    }
    const char *p = name + prefix_len;
    uint32_t value = 0;
    size_t digits = 0;
    for (; *p >= '0' && *p <= '9'; p++) {
        value = value * 10U + (uint32_t)(*p - '0');
        if (++digits > SESSION_MAX_DIGITS) {
            return false;
        }
    }
    if (digits == 0U || *p != '\0' || value == 0U) {
        return false;
    }
    *session_no = value;
    return true;
}
