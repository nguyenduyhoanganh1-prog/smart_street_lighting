/**
 * @file energy_stats.c
 * @brief Energy bookkeeping per lamp (see energy_stats.h).
 */
#include "energy_stats.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SECONDS_PER_HOUR 3600

void energy_stats_init(energy_stats_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->hist_hour = -1;
    }
}

void energy_stats_add(energy_stats_t *s, unsigned lamp, double mwh, bool night)
{
    if (s == NULL || lamp >= ENERGY_LAMPS || !(mwh > 0.0)) {
        return;
    }
    s->total_mwh[lamp] += mwh;
    s->today_mwh[lamp] += mwh;
    s->hist_mwh[lamp][ENERGY_HOURS - 1] += mwh;
    if (night) {
        s->night_mwh[lamp] += mwh;
    }
}

void energy_stats_add_night_time(energy_stats_t *s, double seconds)
{
    if (s != NULL && seconds > 0.0) {
        s->night_s += seconds;
    }
}

static void shift_history(energy_stats_t *s, unsigned hours)
{
    const unsigned keep = ENERGY_HOURS - hours;
    for (unsigned k = 0; k < ENERGY_LAMPS; k++) {
        memmove(&s->hist_mwh[k][0], &s->hist_mwh[k][hours], keep * sizeof(double));
        memset(&s->hist_mwh[k][keep], 0, hours * sizeof(double));
    }
}

unsigned energy_stats_roll(energy_stats_t *s, int64_t epoch_s, int32_t day_key)
{
    if (s == NULL || epoch_s < 0) {
        return 0;
    }
    unsigned flags = 0;

    const int64_t hour = epoch_s / SECONDS_PER_HOUR;
    if (s->hist_hour < 0 || hour < s->hist_hour) {
        s->hist_hour = hour; /* first sync, or clock moved backwards */
    } else if (hour > s->hist_hour) {
        const int64_t gap = hour - s->hist_hour;
        shift_history(s, gap >= ENERGY_HOURS ? (unsigned)ENERGY_HOURS : (unsigned)gap);
        s->hist_hour = hour;
        flags |= ENERGY_ROLL_HOUR;
    }

    if (s->day_key == 0) {
        s->day_key = day_key;
    } else if (day_key != s->day_key) {
        memset(s->today_mwh, 0, sizeof(s->today_mwh));
        s->day_key = day_key;
        flags |= ENERGY_ROLL_DAY;
    }
    return flags;
}

float energy_stats_saving_pct(const energy_stats_t *s, unsigned lamp)
{
    if (s == NULL || lamp >= ENERGY_LAMPS) {
        return NAN;
    }
    const double p100 = (double)s->p100_mw[lamp];
    if (!(p100 > 0.0) || s->night_s < ENERGY_SAVING_MIN_NIGHT_S) {
        return NAN;
    }
    const double full_mwh = p100 * s->night_s / SECONDS_PER_HOUR;
    double pct = (1.0 - s->night_mwh[lamp] / full_mwh) * 100.0;
    if (pct < 0.0) {
        pct = 0.0;
    } else if (pct > 100.0) {
        pct = 100.0;
    }
    return (float)pct;
}

void energy_stats_history(const energy_stats_t *s, uint32_t out[ENERGY_LAMPS][ENERGY_HOURS])
{
    if (s == NULL || out == NULL) {
        return;
    }
    for (unsigned k = 0; k < ENERGY_LAMPS; k++) {
        for (unsigned h = 0; h < ENERGY_HOURS; h++) {
            const double v = s->hist_mwh[k][h];
            out[k][h] = (v > 0.0) ? (uint32_t)llround(v) : 0U;
        }
    }
}
