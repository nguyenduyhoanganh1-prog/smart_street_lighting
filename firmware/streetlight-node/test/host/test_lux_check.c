/**
 * @file test_lux_check.c
 * @brief Host (PC) test of main/lux_check.c, no ESP-IDF needed.
 *
 * Build (from firmware/streetlight-node/test/host):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main -I../../../components/sl_net/include \
 *       test_lux_check.c ../../main/lux_check.c ../../main/lighting.c -o test_lux_check
 */
#include <stdio.h>
#include <string.h>

#include "lux_check.h"

#define SAMPLE_MS  200
#define SUSPECT_MS 120000U /* 2 min */
#define RECOVER_MS 10000U
#define NOON       (12U * 60U)
#define MIDNIGHT   0U

static int s_failures;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                             \
            s_failures++;                                                                \
        }                                                                                \
    } while (0)

static sl_config_t s_cfg;
static int64_t s_now_ms;

static void reset(lux_check_t *lc)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.lux_night = 30;
    s_cfg.lux_day = 100;
    strcpy(s_cfg.night_start, "18:00");
    strcpy(s_cfg.night_end, "06:00");
    lux_check_init(lc);
    s_now_ms = 0;
}

/* Feed the same reading for duration_ms; returns the last verdict */
static bool feed(lux_check_t *lc, float lux, uint16_t minute, bool time_valid, bool lux_ok, uint32_t duration_ms)
{
    bool out = lc->suspect;
    for (uint32_t t = 0; t < duration_ms; t += SAMPLE_MS) {
        s_now_ms += SAMPLE_MS;
        const lighting_input_t in = {
            .now_ms = s_now_ms,
            .lux_ok = lux_ok,
            .lux = lux,
            .time_valid = time_valid,
            .minute_of_day = minute,
            .cfg = &s_cfg,
        };
        out = lux_check_update(lc, &in, SUSPECT_MS, RECOVER_MS);
    }
    return out;
}

static void test_dark_by_day_becomes_suspect_after_the_delay(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(!feed(&lc, 5.0f, NOON, true, true, SUSPECT_MS - SAMPLE_MS));
    CHECK(feed(&lc, 5.0f, NOON, true, true, 2U * SAMPLE_MS));
}

/* A cloud or a hand over the sensor for less than the delay is not suspect */
static void test_short_darkness_restarts_the_delay(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(!feed(&lc, 5.0f, NOON, true, true, SUSPECT_MS - 1000U));
    CHECK(!feed(&lc, 500.0f, NOON, true, true, SAMPLE_MS));
    CHECK(!feed(&lc, 5.0f, NOON, true, true, SUSPECT_MS - 1000U));
}

static void test_dark_at_night_is_normal(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(!feed(&lc, 0.0f, MIDNIGHT, true, true, SUSPECT_MS * 3U));
}

/* Between the thresholds (dusk, overcast) is not "dark" */
static void test_dim_but_above_lux_night_is_not_suspect(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(!feed(&lc, 50.0f, NOON, true, true, SUSPECT_MS * 3U));
}

static void test_without_time_nothing_is_judged(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(!feed(&lc, 0.0f, NOON, false, true, SUSPECT_MS * 3U));
}

/* The night window starting does not clear it: a covered sensor reads dark at night too */
static void test_suspect_kept_through_the_night(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(feed(&lc, 0.0f, NOON, true, true, SUSPECT_MS + 1000U));
    CHECK(feed(&lc, 0.0f, MIDNIGHT, true, true, SUSPECT_MS * 3U));
}

/* Cleared once the sensor sees light again for RECOVER_MS (cover removed, storm over) */
static void test_cleared_when_bright_again(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(feed(&lc, 0.0f, NOON, true, true, SUSPECT_MS + 1000U));
    CHECK(feed(&lc, 500.0f, NOON, true, true, RECOVER_MS - SAMPLE_MS));
    CHECK(!feed(&lc, 500.0f, NOON, true, true, 2U * SAMPLE_MS));
    /* and the delay starts from zero again */
    CHECK(!feed(&lc, 0.0f, NOON, true, true, SUSPECT_MS - 1000U));
}

/* A brief flash while suspect (torch, reflection) does not clear it */
static void test_brief_light_does_not_clear(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(feed(&lc, 0.0f, NOON, true, true, SUSPECT_MS + 1000U));
    CHECK(feed(&lc, 500.0f, NOON, true, true, RECOVER_MS / 2U));
    CHECK(feed(&lc, 0.0f, NOON, true, true, SAMPLE_MS));
    CHECK(feed(&lc, 500.0f, NOON, true, true, RECOVER_MS / 2U));
}

/* Offline sensor: no judgement either way, the verdict is kept */
static void test_offline_sensor_keeps_the_verdict(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(!feed(&lc, 0.0f, NOON, true, false, SUSPECT_MS * 2U));
    CHECK(feed(&lc, 0.0f, NOON, true, true, SUSPECT_MS + 1000U));
    CHECK(feed(&lc, 500.0f, NOON, true, false, RECOVER_MS * 2U));
}

static void test_bad_arguments_do_not_crash(void)
{
    lux_check_t lc;
    reset(&lc);
    CHECK(!lux_check_update(&lc, NULL, SUSPECT_MS, RECOVER_MS));
    CHECK(!lux_check_update(NULL, NULL, SUSPECT_MS, RECOVER_MS));
}

/* lux_check_resolve_night(force, lux_trusted, sensor_night, time_valid, clock_night) */
static void test_clock_day_keeps_lamps_off_even_if_sensor_dark(void)
{
    /* covered BH1750 by day, not yet suspect: no lamps, only the alert later */
    CHECK(!lux_check_resolve_night(false, true, true, true, false));
}

static void test_clock_night_follows_the_sensor(void)
{
    CHECK(lux_check_resolve_night(false, true, true, true, true));
    CHECK(!lux_check_resolve_night(false, true, false, true, true)); /* bright at night: off */
}

static void test_without_time_the_sensor_decides(void)
{
    CHECK(lux_check_resolve_night(false, true, true, false, true));
    CHECK(!lux_check_resolve_night(false, true, false, false, true));
}

static void test_untrusted_sensor_uses_the_clock(void)
{
    CHECK(lux_check_resolve_night(false, false, false, true, true));
    CHECK(!lux_check_resolve_night(false, false, true, true, false));
    CHECK(lux_check_resolve_night(false, false, false, false, true)); /* no time, no sensor: night */
}

static void test_force_night_wins(void)
{
    CHECK(lux_check_resolve_night(true, true, false, true, false));
}

int main(void)
{
    test_clock_day_keeps_lamps_off_even_if_sensor_dark();
    test_clock_night_follows_the_sensor();
    test_without_time_the_sensor_decides();
    test_untrusted_sensor_uses_the_clock();
    test_force_night_wins();
    test_dark_by_day_becomes_suspect_after_the_delay();
    test_short_darkness_restarts_the_delay();
    test_dark_at_night_is_normal();
    test_dim_but_above_lux_night_is_not_suspect();
    test_without_time_nothing_is_judged();
    test_suspect_kept_through_the_night();
    test_cleared_when_bright_again();
    test_brief_light_does_not_clear();
    test_offline_sensor_keeps_the_verdict();
    test_bad_arguments_do_not_crash();
    printf(s_failures == 0 ? "all lux_check tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
