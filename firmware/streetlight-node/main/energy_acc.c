/**
 * @file energy_acc.c
 * @brief Per-lamp energy integrator (see energy_acc.h).
 */
#include "energy_acc.h"

#include <stddef.h>
#include <string.h>

/* mW * us -> mWh: 1 mWh = 1 mW * 3600 s = 3.6e9 mW*us */
#define US_PER_HOUR 3.6e9

void energy_acc_init(energy_acc_t *acc)
{
    if (acc != NULL) {
        memset(acc, 0, sizeof(*acc));
    }
}

double energy_acc_add(energy_acc_t *acc, unsigned channel, float power_mw, int64_t now_us)
{
    if (acc == NULL || channel >= ENERGY_ACC_CHANNELS) {
        return 0.0;
    }
    double mwh = 0.0;
    if (acc->has_last[channel]) {
        const int64_t dt_us = now_us - acc->last_us[channel];
        if (dt_us > 0 && dt_us <= ENERGY_ACC_MAX_GAP_US) {
            /* Negative power only happens with swapped Vin+/Vin-: count as 0 */
            const double p_mw = (power_mw > 0.0f) ? (double)power_mw : 0.0;
            mwh = p_mw * (double)dt_us / US_PER_HOUR;
        }
    }
    acc->last_us[channel] = now_us;
    acc->has_last[channel] = true;
    return mwh;
}

void energy_acc_skip(energy_acc_t *acc, unsigned channel)
{
    if (acc != NULL && channel < ENERGY_ACC_CHANNELS) {
        acc->has_last[channel] = false;
    }
}
