/**
 * @file test_lamp_fault.c
 * @brief Host (PC) test of main/lamp_fault.c, no ESP-IDF needed.
 *
 * Build (from firmware/streetlight-node/test/host):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main \
 *       test_lamp_fault.c ../../main/lamp_fault.c -lm -o test_lamp_fault
 */
#include <stdio.h>

#include "lamp_fault.h"

#define SAMPLE_MS 200     /* control cycle of main.c */
#define P100_MW   54.0f   /* lamp 4 on the model: ~10.5 mA x 5.14 V at 100 % */
#define SUPPLY_V  5.14f

static int s_failures;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                             \
            s_failures++;                                                                \
        }                                                                                \
    } while (0)

static int64_t s_now_ms;

/* A healthy steady lamp at `duty` %, drawing what P100 predicts */
static lamp_fault_in_t lamp(uint8_t duty, float current_ma)
{
    const lamp_fault_in_t in = {
        .ina_ok = true,
        .bus_v = SUPPLY_V,
        .current_ma = current_ma,
        .power_mw = current_ma * SUPPLY_V,
        .duty = duty,
        .steady = true,
        .p100_mw = P100_MW,
        .now_ms = 0,
    };
    return in;
}

/* Feed the same sample n times, one control cycle apart; returns the last verdict */
static lamp_fault_t feed(lamp_fault_state_t *st, lamp_fault_in_t in, unsigned n)
{
    lamp_fault_t out = st->reported;
    for (unsigned k = 0; k < n; k++) {
        s_now_ms += SAMPLE_MS;
        in.now_ms = s_now_ms;
        out = lamp_fault_update(st, &in);
    }
    return out;
}

/* Samples from the first bad one until the fault is reported (the first one starts the 60 s clock) */
#define CONFIRM_RAISE (LAMP_FAULT_RAISE_MS / SAMPLE_MS + 1U)
/* Settle time + one confirmation round: enough samples for any verdict to be reported */
#define ENOUGH (LAMP_FAULT_SETTLE_MS / SAMPLE_MS + CONFIRM_RAISE + 2U)

static void reset(lamp_fault_state_t *st)
{
    lamp_fault_init(st);
    s_now_ms = 0;
}

/* The case from the dashboard: lamp 4 at 20 % drawing 2.1 mA is healthy */
static void test_dim_lamp_with_current_is_ok(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(20, 2.1f), ENOUGH * 3U) == LAMP_FAULT_NONE);
    CHECK(feed(&st, lamp(100, 10.5f), ENOUGH * 3U) == LAMP_FAULT_NONE);
}

/* A reading taken while the lamp still fades up 20 -> 100 % must not be called "open" */
static void test_reading_mid_fade_is_ignored(void)
{
    lamp_fault_state_t st;
    reset(&st);
    lamp_fault_in_t fading = lamp(100, 2.1f); /* target 100 %, current still at the 20 % level */
    fading.steady = false;
    CHECK(feed(&st, fading, ENOUGH * 3U) == LAMP_FAULT_NONE);
}

/* Just after the fade ended the INA219 average may still hold old samples: wait the settle time */
static void test_settle_time_after_fade(void)
{
    lamp_fault_state_t st;
    reset(&st);
    const unsigned settle_samples = LAMP_FAULT_SETTLE_MS / SAMPLE_MS;
    CHECK(feed(&st, lamp(100, 0.0f), settle_samples - 1U) == LAMP_FAULT_NONE);
    CHECK(st.pending_since_ms < 0); /* nothing was judged yet */
}

/* A real open circuit is reported, but only once it lasted LAMP_FAULT_RAISE_MS (1 min) */
static void test_open_circuit_needs_one_minute(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(100, 10.5f), ENOUGH) == LAMP_FAULT_NONE); /* settled and healthy */
    CHECK(feed(&st, lamp(100, 0.0f), CONFIRM_RAISE - 1U) == LAMP_FAULT_NONE); /* 59.8 s: not yet */
    CHECK(feed(&st, lamp(100, 0.0f), 1U) == LAMP_FAULT_OPEN);                 /* 60 s */
}

/* The reported case: odd readings for less than a minute, then normal again -> never reported */
static void test_bad_readings_under_a_minute_are_ignored(void)
{
    lamp_fault_state_t st;
    reset(&st);
    (void)feed(&st, lamp(20, 2.1f), ENOUGH);
    for (unsigned k = 0; k < 5U; k++) {
        CHECK(feed(&st, lamp(20, 0.2f), CONFIRM_RAISE - 6U) == LAMP_FAULT_NONE); /* ~59 s bad */
        CHECK(feed(&st, lamp(20, 2.1f), 1U) == LAMP_FAULT_NONE);                 /* one good sample */
    }
    /* single bad samples (PWM aliasing) among good ones, for minutes */
    for (unsigned k = 0; k < 600U; k++) {
        CHECK(feed(&st, lamp(20, (k % 3U) == 0U ? 0.2f : 2.1f), 1U) == LAMP_FAULT_NONE);
    }
}

/* Once reported, good readings clear it after LAMP_FAULT_CLEAR_MS (faster than raising) */
static void test_clear_after_ten_seconds(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(100, 0.0f), ENOUGH) == LAMP_FAULT_OPEN);
    CHECK(feed(&st, lamp(100, 10.5f), LAMP_FAULT_CLEAR_MS / SAMPLE_MS) == LAMP_FAULT_OPEN);
    CHECK(feed(&st, lamp(100, 10.5f), 1U) == LAMP_FAULT_NONE);
}

/* Off, an open lamp and a good one both read 0: "open" stays until the lamp is lit and draws current */
static void test_open_kept_while_off_cleared_when_lit(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(100, 0.0f), ENOUGH) == LAMP_FAULT_OPEN);
    CHECK(feed(&st, lamp(0, 0.0f), ENOUGH) == LAMP_FAULT_OPEN);
    CHECK(feed(&st, lamp(100, 10.5f), ENOUGH) == LAMP_FAULT_NONE);
}

static void test_stuck_on_when_off_but_current_flows(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(0, 0.0f), ENOUGH) == LAMP_FAULT_NONE);
    CHECK(feed(&st, lamp(0, 8.0f), ENOUGH) == LAMP_FAULT_STUCK_ON);
    CHECK(feed(&st, lamp(0, 0.0f), ENOUGH) == LAMP_FAULT_NONE);
}

/* Without P100 only a (near) zero current means open: a dim but lit lamp is fine */
static void test_without_p100(void)
{
    lamp_fault_state_t st;
    reset(&st);
    lamp_fault_in_t in = lamp(20, 1.0f);
    in.p100_mw = 0.0f;
    CHECK(feed(&st, in, ENOUGH) == LAMP_FAULT_NONE);
    in.current_ma = 0.1f;
    in.power_mw = 0.1f * SUPPLY_V;
    CHECK(feed(&st, in, ENOUGH) == LAMP_FAULT_OPEN);
}

/* A partly failed lamp (one LED of several gone) draws much less than its P100 says */
static void test_partial_failure_against_p100(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(100, 2.0f), ENOUGH) == LAMP_FAULT_OPEN); /* 10 mW vs 54 mW expected */
}

/* Below LAMP_FAULT_MIN_DUTY nothing is judged, so the last verdict stays */
static void test_very_dim_lamp_is_not_judged(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(5, 0.0f), ENOUGH * 3U) == LAMP_FAULT_NONE);
}

/* Sensor, supply and short faults are seen even mid-fade (they do not depend on the duty) */
static void test_hard_faults_ignore_fading(void)
{
    lamp_fault_state_t st;
    reset(&st);
    lamp_fault_in_t in = lamp(100, 10.5f);
    in.steady = false;
    in.ina_ok = false;
    CHECK(feed(&st, in, CONFIRM_RAISE) == LAMP_FAULT_SENSOR);

    reset(&st);
    in = lamp(100, 0.0f);
    in.steady = false;
    in.bus_v = 0.2f;
    CHECK(feed(&st, in, CONFIRM_RAISE) == LAMP_FAULT_NO_SUPPLY);

    reset(&st);
    in = lamp(100, 400.0f);
    in.steady = false;
    CHECK(feed(&st, in, CONFIRM_RAISE) == LAMP_FAULT_SHORT);
}

/* Swapped INA219 wires give a negative current: the lamp is still lit */
static void test_negative_current_counts_as_current(void)
{
    lamp_fault_state_t st;
    reset(&st);
    CHECK(feed(&st, lamp(100, -10.5f), ENOUGH) == LAMP_FAULT_NONE);
}

int main(void)
{
    test_dim_lamp_with_current_is_ok();
    test_reading_mid_fade_is_ignored();
    test_settle_time_after_fade();
    test_open_circuit_needs_one_minute();
    test_bad_readings_under_a_minute_are_ignored();
    test_clear_after_ten_seconds();
    test_open_kept_while_off_cleared_when_lit();
    test_stuck_on_when_off_but_current_flows();
    test_without_p100();
    test_partial_failure_against_p100();
    test_very_dim_lamp_is_not_judged();
    test_hard_faults_ignore_fading();
    test_negative_current_counts_as_current();
    printf(s_failures == 0 ? "all lamp_fault tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
