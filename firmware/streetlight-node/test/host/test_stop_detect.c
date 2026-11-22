/**
 * @file test_stop_detect.c
 * @brief Host (PC) test of main/stop_detect.c, no ESP-IDF needed.
 *
 * Build (from firmware/streetlight-node/test/host):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main test_stop_detect.c ../../main/stop_detect.c -lm \
 *       -o test_stop_detect
 */
#include <stdio.h>

#include "stop_detect.h"

#define FRAME_MS 250       /* FOMO ~4 frames per second */
#define STOP_MS  10000U
#define EPS_PX   30.0f

static int s_failures;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                             \
            s_failures++;                                                                \
        }                                                                                \
    } while (0)

static int64_t s_now_ms;

static stop_obj_t car(float x, float y)
{
    const stop_obj_t o = { .x = x, .y = y, .vehicle = true, .score = 90U, .zone = 2U };
    return o;
}

static stop_obj_t person(float x, float y)
{
    const stop_obj_t o = { .x = x, .y = y, .vehicle = false, .score = 80U, .zone = 3U };
    return o;
}

static void reset(stop_detect_t *sd)
{
    stop_detect_init(sd);
    s_now_ms = 0;
}

static stop_evt_t frame(stop_detect_t *sd, const stop_obj_t *objs, unsigned n)
{
    s_now_ms += FRAME_MS;
    return stop_detect_update(sd, objs, n, s_now_ms, STOP_MS, EPS_PX);
}

/* Same objects for duration_ms; returns the first event seen (NONE if none) */
static stop_evt_t frames(stop_detect_t *sd, const stop_obj_t *objs, unsigned n, uint32_t duration_ms)
{
    stop_evt_t first = { .type = STOP_EVT_NONE };
    for (uint32_t t = 0; t < duration_ms; t += FRAME_MS) {
        const stop_evt_t e = frame(sd, objs, n);
        if (first.type == STOP_EVT_NONE) {
            first = e;
        }
    }
    return first;
}

static void test_still_car_raises_after_stop_time(void)
{
    stop_detect_t sd;
    reset(&sd);
    const stop_obj_t c = car(100.0f, 120.0f);
    CHECK(frames(&sd, &c, 1U, STOP_MS).type == STOP_EVT_NONE); /* first frame starts the clock */
    const stop_evt_t e = frames(&sd, &c, 1U, 2U * FRAME_MS);
    CHECK(e.type == STOP_EVT_RAISE);
    CHECK(e.obj.vehicle && e.obj.zone == 2U && e.obj.score == 90U);
    CHECK(e.obj.x == 100.0f && e.obj.y == 120.0f);
    CHECK(e.still_since_ms == FRAME_MS);
    /* raised once only */
    CHECK(frames(&sd, &c, 1U, STOP_MS).type == STOP_EVT_NONE);
}

/* Cars driving across the frame never stay near one place: a fast one (35 px per frame) and a
 * very slow one (3 px per frame, ~12 px/s: it leaves the 30 px "still" radius every 2.5 s) */
static void test_moving_car_never_raises(void)
{
    stop_detect_t sd;
    reset(&sd);
    for (unsigned k = 0; k < 8U; k++) {
        const stop_obj_t c = car(10.0f + (float)k * 35.0f, 60.0f);
        CHECK(frame(&sd, &c, 1U).type == STOP_EVT_NONE);
    }
    reset(&sd);
    for (unsigned k = 0; k < 90U; k++) {
        const stop_obj_t c = car(10.0f + (float)k * 3.0f, 150.0f);
        CHECK(frame(&sd, &c, 1U).type == STOP_EVT_NONE);
    }
}

/* FOMO centre jitter of one grid cell (~20 px, also diagonally) still counts as still */
static void test_jitter_counts_as_still(void)
{
    stop_detect_t sd;
    reset(&sd);
    stop_evt_t e = { .type = STOP_EVT_NONE };
    for (unsigned k = 0; k < (STOP_MS / FRAME_MS) + 2U && e.type == STOP_EVT_NONE; k++) {
        const float d = (k % 2U) != 0U ? 20.0f : 0.0f;
        const stop_obj_t c = car(100.0f + d, 120.0f + d * 0.75f);
        e = frame(&sd, &c, 1U);
    }
    CHECK(e.type == STOP_EVT_RAISE);
}

/* FOMO misses a few frames: the still time is not restarted */
static void test_missed_frames_keep_the_time(void)
{
    stop_detect_t sd;
    reset(&sd);
    const stop_obj_t c = car(100.0f, 120.0f);
    (void)frames(&sd, &c, 1U, STOP_MS / 2U);
    (void)frames(&sd, NULL, 0U, 2000U); /* < STOP_DETECT_LOST_MS without the car */
    CHECK(frames(&sd, &c, 1U, STOP_MS / 2U + FRAME_MS).type == STOP_EVT_RAISE);
}

static void test_alert_clears_when_object_leaves(void)
{
    stop_detect_t sd;
    reset(&sd);
    const stop_obj_t c = car(100.0f, 120.0f);
    CHECK(frames(&sd, &c, 1U, STOP_MS + 2U * FRAME_MS).type == STOP_EVT_RAISE);
    CHECK(frames(&sd, NULL, 0U, STOP_DETECT_LOST_MS - FRAME_MS).type == STOP_EVT_NONE);
    CHECK(frames(&sd, NULL, 0U, 2U * FRAME_MS).type == STOP_EVT_CLEAR);
}

static void test_alert_clears_when_object_drives_off(void)
{
    stop_detect_t sd;
    reset(&sd);
    const stop_obj_t c = car(100.0f, 120.0f);
    CHECK(frames(&sd, &c, 1U, STOP_MS + 2U * FRAME_MS).type == STOP_EVT_RAISE);
    const stop_obj_t moved = car(140.0f, 120.0f);
    CHECK(frame(&sd, &moved, 1U).type == STOP_EVT_CLEAR);
}

/* "Đã xử lý": no new alert for the same parked car; a car that stops elsewhere later can raise one */
static void test_ack_suppresses_the_same_object(void)
{
    stop_detect_t sd;
    reset(&sd);
    const stop_obj_t c = car(100.0f, 120.0f);
    CHECK(frames(&sd, &c, 1U, STOP_MS + 2U * FRAME_MS).type == STOP_EVT_RAISE);
    stop_detect_ack(&sd);
    CHECK(frames(&sd, &c, 1U, 3U * STOP_MS).type == STOP_EVT_NONE);
    /* it moves 40 px and stops again: a new stop */
    const stop_obj_t c2 = car(140.0f, 120.0f);
    CHECK(frames(&sd, &c2, 1U, STOP_MS + 2U * FRAME_MS).type == STOP_EVT_RAISE);
}

/* A walking person and a parked car: the car is reported, with its own class and place */
static void test_reports_the_stopped_one(void)
{
    stop_detect_t sd;
    reset(&sd);
    stop_evt_t e = { .type = STOP_EVT_NONE };
    for (unsigned k = 0; k < (STOP_MS / FRAME_MS) + 2U && e.type == STOP_EVT_NONE; k++) {
        const stop_obj_t objs[2] = { person(20.0f + (float)k * 4.0f, 60.0f), car(250.0f, 150.0f) };
        e = frame(&sd, objs, 2U);
    }
    CHECK(e.type == STOP_EVT_RAISE);
    CHECK(e.obj.vehicle);
    CHECK(e.obj.x == 250.0f);
}

/* A person next to a parked car is a different object, even when close */
static void test_classes_are_not_mixed(void)
{
    stop_detect_t sd;
    reset(&sd);
    const stop_obj_t c = car(100.0f, 120.0f);
    (void)frames(&sd, &c, 1U, STOP_MS / 2U);
    const stop_obj_t p = person(105.0f, 120.0f);
    (void)frames(&sd, &p, 1U, FRAME_MS); /* person alone for one frame: does not move the car's anchor */
    CHECK(frames(&sd, &c, 1U, STOP_MS / 2U + FRAME_MS).type == STOP_EVT_RAISE);
}

static void test_more_objects_than_tracks(void)
{
    stop_detect_t sd;
    reset(&sd);
    stop_obj_t many[STOP_DETECT_MAX_TRACKS + 4U];
    for (unsigned k = 0; k < STOP_DETECT_MAX_TRACKS + 4U; k++) {
        many[k] = car(10.0f + (float)k * 25.0f, 100.0f);
    }
    CHECK(frames(&sd, many, STOP_DETECT_MAX_TRACKS + 4U, STOP_MS + 2U * FRAME_MS).type == STOP_EVT_RAISE);
    CHECK(stop_detect_update(NULL, many, 1U, 0, STOP_MS, EPS_PX).type == STOP_EVT_NONE);
    CHECK(stop_detect_update(&sd, NULL, 1U, 0, STOP_MS, EPS_PX).type == STOP_EVT_NONE);
}

/* Each vehicle in the alert picture gets its own still time, acknowledged ones included */
static void test_still_since_per_object(void)
{
    stop_detect_t sd;
    reset(&sd);
    const stop_obj_t a = car(100.0f, 120.0f);
    (void)frames(&sd, &a, 1U, 5000U);                 /* car A alone for 5 s */
    const stop_obj_t both[2] = { a, car(250.0f, 150.0f) };
    (void)frames(&sd, both, 2U, STOP_MS + 2U * FRAME_MS); /* A raises; B arrives 5 s later */
    stop_detect_ack(&sd);
    (void)frames(&sd, both, 2U, 1000U);

    int64_t since_a = -1;
    int64_t since_b = -1;
    CHECK(stop_detect_still_since(&sd, 105.0f, 118.0f, true, &since_a)); /* a little off: still matched */
    CHECK(stop_detect_still_since(&sd, 250.0f, 150.0f, true, &since_b));
    CHECK(since_a == FRAME_MS);
    CHECK(since_b == 5000 + FRAME_MS);
    CHECK(!stop_detect_still_since(&sd, 250.0f, 150.0f, false, &since_b)); /* no person there */
    CHECK(!stop_detect_still_since(&sd, 20.0f, 20.0f, true, &since_b));    /* nothing there */
    CHECK(!stop_detect_still_since(NULL, 0.0f, 0.0f, true, &since_b));
}

int main(void)
{
    test_still_since_per_object();
    test_still_car_raises_after_stop_time();
    test_moving_car_never_raises();
    test_jitter_counts_as_still();
    test_missed_frames_keep_the_time();
    test_alert_clears_when_object_leaves();
    test_alert_clears_when_object_drives_off();
    test_ack_suppresses_the_same_object();
    test_reports_the_stopped_one();
    test_classes_are_not_mixed();
    test_more_objects_than_tracks();
    printf(s_failures == 0 ? "all stop_detect tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
