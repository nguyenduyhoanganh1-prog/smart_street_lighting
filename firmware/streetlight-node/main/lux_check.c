/**
 * @file lux_check.c
 * @brief BH1750 plausibility check (see lux_check.h).
 */
#include "lux_check.h"

#include <stddef.h>

void lux_check_init(lux_check_t *lc)
{
    lc->suspect = false;
    lc->dark_since_ms = -1;
    lc->bright_since_ms = -1;
}

/* true once `cond` has held continuously for hold_ms; *since_ms tracks the start (-1 = not holding) */
static bool held_for(bool cond, int64_t *since_ms, int64_t now_ms, uint32_t hold_ms)
{
    if (!cond) {
        *since_ms = -1;
        return false;
    }
    if (*since_ms < 0) {
        *since_ms = now_ms;
    }
    return now_ms - *since_ms >= (int64_t)hold_ms;
}

bool lux_check_update(lux_check_t *lc, const lighting_input_t *in, uint32_t suspect_ms, uint32_t recover_ms)
{
    if (lc == NULL || in == NULL || in->cfg == NULL) {
        return false;
    }
    if (!in->lux_ok) {
        /* an offline sensor is handled by the caller as a fault already; keep the verdict */
        lc->dark_since_ms = -1;
        lc->bright_since_ms = -1;
        return lc->suspect;
    }

    if (lc->suspect) {
        const bool bright = in->lux > (float)in->cfg->lux_day;
        if (held_for(bright, &lc->bright_since_ms, in->now_ms, recover_ms)) {
            lc->suspect = false; /* the sensor sees light again: it works */
            lc->bright_since_ms = -1;
            lc->dark_since_ms = -1;
        }
        return lc->suspect;
    }

    const bool day_by_clock = in->time_valid && !lighting_in_night_window(in->minute_of_day, in->cfg);
    const bool dark = in->lux < (float)in->cfg->lux_night;
    if (held_for(day_by_clock && dark, &lc->dark_since_ms, in->now_ms, suspect_ms)) {
        lc->suspect = true;
        lc->dark_since_ms = -1;
        lc->bright_since_ms = -1;
    }
    return lc->suspect;
}

bool lux_check_resolve_night(bool force_night, bool lux_trusted, bool sensor_night, bool time_valid,
                             bool clock_night)
{
    if (force_night) {
        return true;
    }
    if (time_valid && !clock_night) {
        return false; /* day by the clock: never light up, a dark sensor only raises the alert */
    }
    if (lux_trusted) {
        return sensor_night;
    }
    return !time_valid || clock_night;
}
