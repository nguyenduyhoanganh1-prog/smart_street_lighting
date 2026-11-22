/**
 * @file ina_zero.h
 * @brief Automatic zero-offset calibration of one INA219 channel. Pure logic: no ESP-IDF, unit-tested
 *        on the PC (test/host/test_ina_zero.c).
 *
 * Why: the INA219 shunt amplifier has an input offset (datasheet: +-10 uV typical, +-100 uV max, different
 * on every module). With the 0.1 ohm shunt that is +-0.1..1 mA. It shows as a negative current while the
 * lamp is off, or as "ghost" energy all day on a module with a positive offset, and it is a ~10 % error at
 * the 20 % base level (2.1 mA).
 *
 * How: while the lamp is commanded off and steady for INA_ZERO_SETTLE_MS, the true current is 0, so the
 * average reading IS the offset. INA_ZERO_WINDOW samples give one estimate; the first one is used as is,
 * later ones are blended in (INA_ZERO_BLEND) to follow temperature drift without jumps. Readings larger than
 * INA_ZERO_MAX_MA are real current (e.g. a stuck ULN2003) and are never learnt.
 * Until the first estimate exists the readings pass through unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INA_ZERO_SETTLE_MS  1000     /* off this long: fade finished and the 68 ms INA219 average is clean */
#define INA_ZERO_WINDOW     50U      /* samples per estimate; 50 x 200 ms = 10 s */
#define INA_ZERO_MAX_MA     1.5f     /* larger readings are real current, not offset */
#define INA_ZERO_BLEND      0.25f    /* weight of a new estimate once calibrated */
#define INA_ZERO_BAND_MA    0.15f    /* after correction, |I| below this is noise (1 LSB = 0.1 mA) -> 0 */

typedef struct {
    bool valid;              /* at least one estimate made */
    float offset_ma;         /* subtracted from every reading once valid */
    int64_t off_since_ms;    /* -1 = lamp not steadily off */
    double sum_ma;
    uint32_t samples;
} ina_zero_t;

void ina_zero_init(ina_zero_t *z);

/**
 * Feed one RAW reading (call every sensor cycle, before ina_zero_correct).
 * @param lamp_off  lamp commanded to 0 % and no fade running
 * @return true when this sample completed a new estimate (for logging)
 */
bool ina_zero_update(ina_zero_t *z, bool ina_ok, float raw_current_ma, bool lamp_off, int64_t now_ms);

/** Offset-corrected current, mA (unchanged until calibrated). */
float ina_zero_correct(const ina_zero_t *z, float raw_current_ma);

#ifdef __cplusplus
}
#endif
