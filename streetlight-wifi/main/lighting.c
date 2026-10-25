/**
 * @file lighting.c
 * @brief Lighting rules without AI (see lighting.h).
 */
#include "lighting.h"

#include <ctype.h>
#include <stddef.h>

#define MINUTES_PER_HOUR 60U
#define HOURS_PER_DAY    24U

bool lighting_parse_hhmm(const char *hhmm, uint16_t *minutes)
{
    if (hhmm == NULL || minutes == NULL) {
        return false;
    }
    for (unsigned i = 0; i < 5U; i++) {
        const bool ok = (i == 2U) ? (hhmm[i] == ':') : (isdigit((unsigned char)hhmm[i]) != 0);
        if (!ok) {
            return false;
        }
    }
    if (hhmm[5] != '\0') {
        return false;
    }
    const unsigned h = (unsigned)(hhmm[0] - '0') * 10U + (unsigned)(hhmm[1] - '0');
    const unsigned m = (unsigned)(hhmm[3] - '0') * 10U + (unsigned)(hhmm[4] - '0');
    if (h >= HOURS_PER_DAY || m >= MINUTES_PER_HOUR) {
        return false;
    }
    *minutes = (uint16_t)(h * MINUTES_PER_HOUR + m);
    return true;
}

bool lighting_in_night_window(uint16_t minute_of_day, const sl_config_t *cfg)
{
    uint16_t start = 0;
    uint16_t end = 0;
    if (cfg == NULL || !lighting_parse_hhmm(cfg->night_start, &start)
        || !lighting_parse_hhmm(cfg->night_end, &end)) {
        return true; /* unknown window: safer to light the road */
    }
    if (start < end) {
        return minute_of_day >= start && minute_of_day < end;
    }
    /* Window crosses midnight, e.g. 18:00 -> 06:00 */
    return minute_of_day >= start || minute_of_day < end;
}

bool daynight_update(daynight_t *dn, const lighting_input_t *in, uint32_t hold_ms)
{
    if (dn == NULL || in == NULL || in->cfg == NULL) {
        return true;
    }
    if (!in->lux_ok) {
        dn->flip_pending = false;
        return dn->initialized ? dn->night : true;
    }
    const bool dark = in->lux < (float)in->cfg->lux_night;
    const bool bright = in->lux > (float)in->cfg->lux_day;

    if (!dn->initialized) {
        dn->initialized = true;
        dn->flip_pending = false;
        if (dark) {
            dn->night = true;
        } else if (bright) {
            dn->night = false;
        } else {
            /* Between thresholds at boot: use the clock, or night when unknown */
            dn->night = !in->time_valid || lighting_in_night_window(in->minute_of_day, in->cfg);
        }
        return dn->night;
    }

    const bool wants_flip = dn->night ? bright : dark;
    if (!wants_flip) {
        dn->flip_pending = false;
    } else if (!dn->flip_pending) {
        dn->flip_pending = true;
        dn->flip_since_ms = in->now_ms;
    } else if (in->now_ms - dn->flip_since_ms >= (int64_t)hold_ms) {
        dn->night = !dn->night;
        dn->flip_pending = false;
    }
    return dn->night;
}

lighting_decision_t lighting_decide(const lighting_input_t *in, bool night,
                                    uint8_t base_dim, uint8_t full)
{
    lighting_decision_t d = { .duty = full, .mode = SL_MODE_SCHEDULE };
    if (in == NULL) {
        return d; /* fail safe: lights on */
    }
    if (!in->lux_ok) {
        /* Caller passes night = forced || no time || inside the night window */
        d.duty = night ? full : 0U;
        return d;
    }
    d.mode = SL_MODE_AUTO;
    d.duty = night ? base_dim : 0U;
    return d;
}
