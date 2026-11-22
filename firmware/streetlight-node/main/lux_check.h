/**
 * @file lux_check.h
 * @brief Plausibility check of the BH1750: "dark" for a long time while the clock says it is day.
 *
 * Pure logic (no ESP-IDF), unit-tested on the PC (test/host/test_lux_check.c).
 *
 * A covered (leaf, dust, object) or stuck BH1750 still answers on I2C, so it is not seen as offline,
 * and its low lux would keep the lamps on all day. When, by the SNTP clock, it is day (outside the
 * night window) and lux stays below lux_night for suspect_ms, the reading is "suspect": the caller
 * then treats the BH1750 as faulty (schedule mode, lamps off by day) and raises an alert.
 * Chosen trade-off: a real storm darkness by day also leaves the lamps off.
 *
 * Cleared only when the sensor sees light again (lux > lux_day for recover_ms): the night window
 * alone does not clear it, a covered sensor reads "dark" at night too.
 * Without a valid time nothing is judged (day and night cannot be told apart).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lighting.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool suspect;
    int64_t dark_since_ms;   /* -1 = not dark by day right now */
    int64_t bright_since_ms; /* -1 = not bright right now */
} lux_check_t;

void lux_check_init(lux_check_t *lc);

/**
 * Feed one sample (every sensor cycle).
 * @return true while the BH1750 reading is suspect
 */
bool lux_check_update(lux_check_t *lc, const lighting_input_t *in, uint32_t suspect_ms, uint32_t recover_ms);

/**
 * Final day/night choice for the lamps.
 *
 * When the SNTP clock says day, it is day: a dark BH1750 by day does not switch the lamps on,
 * it only leads to the "bh1750" alert once lux_check_update() turns suspect. At night by the clock,
 * or without a valid time, a trusted BH1750 decides; an untrusted one falls back to the clock
 * (night when the time is unknown).
 *
 * @param force_night  dashboard "force night"
 * @param lux_trusted  BH1750 online and not suspect
 * @param sensor_night day/night from the BH1750 (with hysteresis)
 * @param time_valid   SNTP synced
 * @param clock_night  inside the night window (ignored when !time_valid)
 * @return true when the lamps should run the night rules
 */
bool lux_check_resolve_night(bool force_night, bool lux_trusted, bool sensor_night, bool time_valid,
                             bool clock_night);

#ifdef __cplusplus
}
#endif
