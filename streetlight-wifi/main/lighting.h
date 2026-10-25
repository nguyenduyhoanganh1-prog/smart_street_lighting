/**
 * @file lighting.h
 * @brief Lighting rules without AI (plans/01 section 3): day/night with
 *        hysteresis + hold time, and the time-based fallback mode.
 *
 * Pure logic (no ESP-IDF calls) so it can be unit-tested on a PC.
 * Zone rules (person/vehicle) will be added with the camera + FOMO model.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sl_mqtt_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t now_ms;            /* monotonic time */
    bool lux_ok;               /* false -> BH1750 failed -> schedule mode */
    float lux;
    bool time_valid;           /* SNTP synced */
    uint16_t minute_of_day;    /* local time 0..1439, valid if time_valid */
    const sl_config_t *cfg;
} lighting_input_t;

/** Day/night state machine. Zero-initialise before the first update. */
typedef struct {
    bool initialized;
    bool night;
    bool flip_pending;
    int64_t flip_since_ms;
} daynight_t;

typedef struct {
    uint8_t duty;              /* same duty for all lamps, % */
    sl_mode_t mode;            /* SL_MODE_AUTO or SL_MODE_SCHEDULE */
} lighting_decision_t;

/** Parse "HH:MM" into minutes since midnight. */
bool lighting_parse_hhmm(const char *hhmm, uint16_t *minutes);

/** true when minute_of_day lies in [night_start, night_end), crossing midnight if needed. */
bool lighting_in_night_window(uint16_t minute_of_day, const sl_config_t *cfg);

/**
 * Update day/night from lux. Changes only after the opposite threshold has
 * been crossed continuously for hold_ms. At boot, lux between the thresholds
 * is resolved by the clock (or "night" when the time is unknown).
 * Keeps the previous decision when lux is not available.
 */
bool daynight_update(daynight_t *dn, const lighting_input_t *in, uint32_t hold_ms);

/**
 * Brightness for all lamps when not in manual mode.
 * BH1750 ok:     night -> base_dim, day -> 0   (mode auto)
 * BH1750 failed: night -> full,     day -> 0   (mode schedule)
 * @param night  BH1750 ok: day/night decision (or forced).
 *               BH1750 failed: forced || no time || inside the night window.
 */
lighting_decision_t lighting_decide(const lighting_input_t *in, bool night,
                                    uint8_t base_dim, uint8_t full);

#ifdef __cplusplus
}
#endif
