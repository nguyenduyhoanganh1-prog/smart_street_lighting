/**
 * @file test_ina_zero.c
 * @brief Host (PC) test of main/ina_zero.c, no ESP-IDF needed.
 *
 * Build (from firmware/streetlight-node/test/host):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main test_ina_zero.c ../../main/ina_zero.c -lm -o test_ina_zero
 */
#include <math.h>
#include <stdio.h>

#include "ina_zero.h"

#define SAMPLE_MS 200

static int s_failures;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                             \
            s_failures++;                                                                \
        }                                                                                \
    } while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 0.005f)

static int64_t s_now_ms;

static void reset(ina_zero_t *z)
{
    ina_zero_init(z);
    s_now_ms = 0;
}

static void feed(ina_zero_t *z, float raw_ma, bool lamp_off, unsigned n)
{
    for (unsigned k = 0; k < n; k++) {
        s_now_ms += SAMPLE_MS;
        (void)ina_zero_update(z, true, raw_ma, lamp_off, s_now_ms);
    }
}

/* Samples needed from "lamp off" to the first estimate */
#define SETTLE_SAMPLES (INA_ZERO_SETTLE_MS / SAMPLE_MS)
#define FIRST_ESTIMATE (SETTLE_SAMPLES + INA_ZERO_WINDOW)

static void test_uncalibrated_readings_pass_through(void)
{
    ina_zero_t z;
    reset(&z);
    CHECK(NEAR(ina_zero_correct(&z, -0.2f), -0.2f));
    CHECK(NEAR(ina_zero_correct(&z, 2.1f), 2.1f));
}

/* The reported case: lamp off by day reads -0.2 mA -> learnt, then shown as 0, and a lit lamp is corrected */
static void test_negative_offset_is_learnt_and_removed(void)
{
    ina_zero_t z;
    reset(&z);
    feed(&z, -0.2f, true, FIRST_ESTIMATE - 1U);
    CHECK(!z.valid);
    feed(&z, -0.2f, true, 1U);
    CHECK(z.valid);
    CHECK(NEAR(z.offset_ma, -0.2f));
    CHECK(ina_zero_correct(&z, -0.2f) == 0.0f);
    CHECK(NEAR(ina_zero_correct(&z, 1.9f), 2.1f)); /* 20 % lamp: true 2.1 mA read as 1.9 */
}

/* A positive offset would count "ghost" energy all day: removed as well */
static void test_positive_offset_is_removed(void)
{
    ina_zero_t z;
    reset(&z);
    feed(&z, 0.3f, true, FIRST_ESTIMATE);
    CHECK(NEAR(z.offset_ma, 0.3f));
    CHECK(ina_zero_correct(&z, 0.3f) == 0.0f);
}

/* +-1 LSB noise around the offset averages out */
static void test_noise_averages_out(void)
{
    ina_zero_t z;
    reset(&z);
    for (unsigned k = 0; k < FIRST_ESTIMATE; k++) {
        feed(&z, (k % 2U) != 0U ? -0.1f : -0.3f, true, 1U);
    }
    CHECK(z.valid);
    CHECK(fabsf(z.offset_ma + 0.2f) < 0.02f);
    CHECK(ina_zero_correct(&z, -0.1f) == 0.0f); /* residual noise shown as 0 */
}

/* Readings right after switching off (fade tail, INA219 still averaging) are not used */
static void test_settle_time_is_skipped(void)
{
    ina_zero_t z;
    reset(&z);
    feed(&z, -1.2f, true, SETTLE_SAMPLES); /* tail: still some current in the average */
    feed(&z, -0.2f, true, INA_ZERO_WINDOW);
    CHECK(z.valid);
    CHECK(NEAR(z.offset_ma, -0.2f));
}

/* The lamp switching on mid-window drops that window: no mix of on and off readings */
static void test_lamp_on_drops_the_window(void)
{
    ina_zero_t z;
    reset(&z);
    feed(&z, -0.2f, true, FIRST_ESTIMATE - 5U);
    feed(&z, 2.1f, false, 3U);
    feed(&z, -0.2f, true, FIRST_ESTIMATE - 1U);
    CHECK(!z.valid);
    feed(&z, -0.2f, true, 1U);
    CHECK(z.valid);
    CHECK(NEAR(z.offset_ma, -0.2f));
}

/* Current while "off" (stuck ULN2003) is real: never learnt as offset */
static void test_real_current_is_never_learnt(void)
{
    ina_zero_t z;
    reset(&z);
    feed(&z, 8.0f, true, FIRST_ESTIMATE * 3U);
    CHECK(!z.valid);
    CHECK(NEAR(ina_zero_correct(&z, 8.0f), 8.0f));
}

/* Later estimates are blended in (temperature drift) instead of replacing the offset */
static void test_drift_is_blended(void)
{
    ina_zero_t z;
    reset(&z);
    feed(&z, -0.2f, true, FIRST_ESTIMATE);
    feed(&z, 0.2f, true, INA_ZERO_WINDOW);
    CHECK(NEAR(z.offset_ma, -0.2f + INA_ZERO_BLEND * 0.4f));
}

/* The noise band does not hide a real dim lamp (10 % ~ 1 mA) */
static void test_dim_lamp_is_not_zeroed(void)
{
    ina_zero_t z;
    reset(&z);
    feed(&z, -0.2f, true, FIRST_ESTIMATE);
    CHECK(NEAR(ina_zero_correct(&z, 0.85f), 1.05f));
}

static void test_sensor_offline_and_null(void)
{
    ina_zero_t z;
    reset(&z);
    for (unsigned k = 0; k < FIRST_ESTIMATE * 2U; k++) {
        s_now_ms += SAMPLE_MS;
        (void)ina_zero_update(&z, false, -0.2f, true, s_now_ms);
    }
    CHECK(!z.valid);
    CHECK(!ina_zero_update(NULL, true, 0.0f, true, 0));
    CHECK(NEAR(ina_zero_correct(NULL, 1.0f), 1.0f));
}

int main(void)
{
    test_uncalibrated_readings_pass_through();
    test_negative_offset_is_learnt_and_removed();
    test_positive_offset_is_removed();
    test_noise_averages_out();
    test_settle_time_is_skipped();
    test_lamp_on_drops_the_window();
    test_real_current_is_never_learnt();
    test_drift_is_blended();
    test_dim_lamp_is_not_zeroed();
    test_sensor_offline_and_null();
    printf(s_failures == 0 ? "all ina_zero tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
