/**
 * @file energy_stats.h
 * @brief Energy bookkeeping per lamp (plans/01 section 4):
 *        total, today (reset at local midnight), 24-hour history,
 *        and saving % = 1 - E_night / (P100 * night_time).
 *
 * Pure logic (no ESP-IDF calls). The whole struct is persisted to NVS as a blob,
 * so keep it plain data and bump NODE_STORE_ENERGY_VERSION when it changes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENERGY_LAMPS 4
#define ENERGY_HOURS 24
#define ENERGY_SAVING_MIN_NIGHT_S 60.0 /* below this the saving % is not meaningful */

#define ENERGY_ROLL_HOUR (1U << 0)
#define ENERGY_ROLL_DAY  (1U << 1)

typedef struct {
    double total_mwh[ENERGY_LAMPS];
    double today_mwh[ENERGY_LAMPS];
    double hist_mwh[ENERGY_LAMPS][ENERGY_HOURS]; /* [k][23] = current hour */
    int64_t hist_hour;                           /* Unix hour of [k][23]; -1 = unknown */
    int32_t day_key;                             /* local yyyymmdd of "today"; 0 = unknown */
    double night_mwh[ENERGY_LAMPS];              /* energy used while night */
    double night_s;                              /* total night time, s */
    float p100_mw[ENERGY_LAMPS];                 /* measured power at 100 %; 0 = not calibrated */
} energy_stats_t;

void energy_stats_init(energy_stats_t *s);

/** Book energy for one lamp. */
void energy_stats_add(energy_stats_t *s, unsigned lamp, double mwh, bool night);

void energy_stats_add_night_time(energy_stats_t *s, double seconds);

/**
 * Advance the hour ring and the "today" counters to the given time.
 * @param epoch_s  Unix time (UTC); Vietnam is UTC+7, so local hours line up
 * @param day_key  local date as yyyymmdd
 * @return ENERGY_ROLL_* flags
 */
unsigned energy_stats_roll(energy_stats_t *s, int64_t epoch_s, int32_t day_key);

/** Saving % of one lamp (0..100), or NAN when P100 or night time is missing. */
float energy_stats_saving_pct(const energy_stats_t *s, unsigned lamp);

/** History rounded to whole mWh, oldest hour first. */
void energy_stats_history(const energy_stats_t *s, uint32_t out[ENERGY_LAMPS][ENERGY_HOURS]);

#ifdef __cplusplus
}
#endif
