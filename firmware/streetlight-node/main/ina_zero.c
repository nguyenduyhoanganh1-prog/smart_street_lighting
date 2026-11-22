/**
 * @file ina_zero.c
 * @brief INA219 zero-offset calibration (see ina_zero.h).
 */
#include "ina_zero.h"

#include <math.h>
#include <stddef.h>

static void restart_window(ina_zero_t *z)
{
    z->sum_ma = 0.0;
    z->samples = 0U;
}

void ina_zero_init(ina_zero_t *z)
{
    z->valid = false;
    z->offset_ma = 0.0f;
    z->off_since_ms = -1;
    restart_window(z);
}

bool ina_zero_update(ina_zero_t *z, bool ina_ok, float raw_current_ma, bool lamp_off, int64_t now_ms)
{
    if (z == NULL) {
        return false;
    }
    if (!ina_ok || !lamp_off) {
        z->off_since_ms = -1; /* a partial window is dropped: only clean "off" stretches count */
        restart_window(z);
        return false;
    }
    if (z->off_since_ms < 0) {
        z->off_since_ms = now_ms;
    }
    if (now_ms - z->off_since_ms < (int64_t)INA_ZERO_SETTLE_MS) {
        return false;
    }
    if (fabsf(raw_current_ma) > INA_ZERO_MAX_MA) {
        restart_window(z); /* real current while "off": never learn it as offset */
        return false;
    }

    z->sum_ma += (double)raw_current_ma;
    z->samples++;
    if (z->samples < INA_ZERO_WINDOW) {
        return false;
    }
    const float estimate = (float)(z->sum_ma / (double)z->samples);
    if (z->valid) {
        z->offset_ma += INA_ZERO_BLEND * (estimate - z->offset_ma);
    } else {
        z->offset_ma = estimate;
        z->valid = true;
    }
    restart_window(z);
    return true;
}

float ina_zero_correct(const ina_zero_t *z, float raw_current_ma)
{
    if (z == NULL || !z->valid) {
        return raw_current_ma;
    }
    const float corrected = raw_current_ma - z->offset_ma;
    return fabsf(corrected) < INA_ZERO_BAND_MA ? 0.0f : corrected;
}
