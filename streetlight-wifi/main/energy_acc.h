/**
 * @file energy_acc.h
 * @brief Per-lamp energy integrator: dE = P * dt (rectangle rule).
 *
 * Returns the energy added by each sample; energy_stats books it into
 * today / total / history. Pure C (no ESP-IDF calls) so it can be unit-tested.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENERGY_ACC_CHANNELS 4
#define ENERGY_ACC_MAX_GAP_US (2LL * 1000000LL) /* longer gaps are not integrated */

typedef struct {
    int64_t last_us[ENERGY_ACC_CHANNELS];
    bool has_last[ENERGY_ACC_CHANNELS];
} energy_acc_t;

void energy_acc_init(energy_acc_t *acc);

/**
 * Add one power sample (mW) taken at now_us (monotonic microseconds).
 * @return energy since the previous sample in mWh (0 for the first sample or a gap)
 */
double energy_acc_add(energy_acc_t *acc, unsigned channel, float power_mw, int64_t now_us);

/** Mark a missing sample: the gap until the next valid sample is not integrated. */
void energy_acc_skip(energy_acc_t *acc, unsigned channel);

#ifdef __cplusplus
}
#endif
