/**
 * @file lamp_fault.h
 * @brief Lamp fault detection from the INA219 reading of each lamp. Pure logic: no ESP-IDF, unit-tested
 *        on the PC (test/host/test_lamp_fault.c).
 *
 * Why on the board and not in the dashboard: only the board knows whether a lamp is still fading
 * (a reading taken mid-fade does not match the reported duty) and what the lamp really draws at
 * 100 % (P100, self-calibrated). A fixed "20 mA per lamp" guess in the dashboard flagged healthy
 * lamps that draw ~10 mA at 100 %.
 *
 * Rules, judged only once the lamp has been steady for LAMP_FAULT_SETTLE_MS:
 *   INA219 not answering            -> SENSOR     (judged at any time)
 *   bus voltage < LAMP_FAULT_LOW_V  -> NO_SUPPLY  (judged at any time)
 *   current > LAMP_FAULT_SHORT_MA   -> SHORT      (judged at any time)
 *   duty 0 and current > STUCK_MA   -> STUCK_ON   (ULN2003 channel stuck on)
 *   duty >= MIN_DUTY and too little -> OPEN       ("too little": power < OPEN_RATIO x P100 x duty,
 *                                                 or current < OPEN_MIN_MA when P100 is unknown)
 *   1 % .. MIN_DUTY-1 %             -> no verdict (too little current to tell): keep the last one
 * A fault is reported only after the same verdict held for LAMP_FAULT_RAISE_MS (1 min: short glitches,
 * fades and PWM aliasing never get that far); back to "ok" after LAMP_FAULT_CLEAR_MS of good readings.
 * Samples that cannot tell (fading, very dim) neither confirm nor break the running confirmation.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAMP_FAULT_SETTLE_MS   600     /* steady this long before judging (INA219 average = 68 ms) */
#define LAMP_FAULT_RAISE_MS    60000   /* a fault must last this long before it is reported */
#define LAMP_FAULT_CLEAR_MS    10000   /* good readings this long -> fault cleared */
#define LAMP_FAULT_LOW_V       4.0f    /* lamp supply is 5 V */
#define LAMP_FAULT_SHORT_MA    100.0f  /* one lamp draws ~10-20 mA at 100 % */
#define LAMP_FAULT_STUCK_MA    2.0f    /* lamp off but current flows */
#define LAMP_FAULT_MIN_DUTY    10U     /* below this the current is too small to judge "open" */
#define LAMP_FAULT_OPEN_RATIO  0.3f    /* power below 30 % of the expected one -> open */
#define LAMP_FAULT_OPEN_MIN_MA 0.5f    /* P100 unknown: only a (near) zero current means open */

/** Same codes as the MQTT "fault" field and the dashboard FAULT_TEXT. */
typedef enum {
    LAMP_FAULT_NONE = 0,
    LAMP_FAULT_OPEN = 1,      /* lamp on, no current: open circuit / burnt LED */
    LAMP_FAULT_SHORT = 2,     /* over-current / short circuit */
    LAMP_FAULT_STUCK_ON = 3,  /* lamp off, current flows: check the ULN2003 */
    LAMP_FAULT_NO_SUPPLY = 4, /* lamp 5 V supply missing */
    LAMP_FAULT_SENSOR = 5,    /* this lamp's INA219 does not answer */
} lamp_fault_t;

/** One sample of one lamp. */
typedef struct {
    bool ina_ok;
    float bus_v;
    float current_ma;
    float power_mw;
    uint8_t duty;     /**< target brightness, 0..100 % */
    bool steady;      /**< no fade running or pending (lamp_pwm_is_steady) */
    float p100_mw;    /**< calibrated power at 100 %; <= 0 = not calibrated yet */
    int64_t now_ms;
} lamp_fault_in_t;

typedef struct {
    lamp_fault_t reported;    /* what is sent to the dashboard */
    lamp_fault_t pending;     /* candidate verdict being confirmed */
    int64_t pending_since_ms; /* `pending` seen since then without a contrary sample, -1 = none */
    int64_t steady_since_ms;  /* -1 = fading */
} lamp_fault_state_t;

void lamp_fault_init(lamp_fault_state_t *st);

/**
 * Feed one sample (call every sensor cycle).
 * @return the reported verdict after this sample
 */
lamp_fault_t lamp_fault_update(lamp_fault_state_t *st, const lamp_fault_in_t *in);

#ifdef __cplusplus
}
#endif
