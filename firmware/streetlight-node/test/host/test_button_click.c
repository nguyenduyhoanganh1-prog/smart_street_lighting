/**
 * @file test_button_click.c
 * @brief Host (PC) test of main/button_click.c, no ESP-IDF needed.
 *
 * Build (from firmware/streetlight-node/test/host):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main test_button_click.c ../../main/button_click.c \
 *       -o test_button_click
 */
#include <stdio.h>

#include "button_click.h"

#define POLL_MS 10

static int s_failures;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                             \
            s_failures++;                                                                \
        }                                                                                \
    } while (0)

/* Test driver: feeds the detector one sample every POLL_MS and counts the events it returns. */
typedef struct {
    button_click_t det;
    int64_t now_ms;
    int doubles;
} rig_t;

static void rig_init(rig_t *r)
{
    button_click_init(&r->det);
    r->now_ms = 1000; /* any start time: the detector must not depend on t = 0 */
    r->doubles = 0;
}

static void rig_hold(rig_t *r, bool pressed, int64_t duration_ms)
{
    for (int64_t t = 0; t < duration_ms; t += POLL_MS) {
        if (button_click_feed(&r->det, pressed, r->now_ms) == BUTTON_EVT_DOUBLE_CLICK) {
            r->doubles++;
        }
        r->now_ms += POLL_MS;
    }
}

static void test_two_quick_clicks_give_one_double(void)
{
    rig_t r;
    rig_init(&r);
    rig_hold(&r, false, 200);
    rig_hold(&r, true, 100);
    rig_hold(&r, false, 150);
    rig_hold(&r, true, 100);
    CHECK(r.doubles == 0); /* the event comes on the release of the 2nd click, not on its press */
    rig_hold(&r, false, 200);
    CHECK(r.doubles == 1);
}

static void test_single_click_gives_nothing(void)
{
    rig_t r;
    rig_init(&r);
    rig_hold(&r, true, 100);
    rig_hold(&r, false, 2000);
    CHECK(r.doubles == 0);
}

static void test_clicks_too_far_apart_are_not_a_double(void)
{
    rig_t r;
    rig_init(&r);
    rig_hold(&r, true, 100);
    rig_hold(&r, false, 1500); /* longer than the double-click gap */
    rig_hold(&r, true, 100);
    rig_hold(&r, false, 500);
    CHECK(r.doubles == 0);
}

static void test_slow_pair_then_fast_pair_counts_the_fast_one(void)
{
    rig_t r;
    rig_init(&r);
    rig_hold(&r, true, 100);
    rig_hold(&r, false, 1500);
    rig_hold(&r, true, 100); /* this click starts a new pair ... */
    rig_hold(&r, false, 150);
    rig_hold(&r, true, 100); /* ... and this one completes it */
    rig_hold(&r, false, 300);
    CHECK(r.doubles == 1);
}

static void test_long_press_is_not_a_click(void)
{
    rig_t r;
    rig_init(&r);
    rig_hold(&r, true, 100);
    rig_hold(&r, false, 150);
    rig_hold(&r, true, 1500); /* held: not a click */
    rig_hold(&r, false, 500);
    CHECK(r.doubles == 0);
}

static void test_contact_bounce_is_ignored(void)
{
    rig_t r;
    rig_init(&r);
    /* One physical click whose contacts chatter for ~10 ms on press and on release */
    for (int click = 0; click < 2; click++) {
        rig_hold(&r, true, POLL_MS);
        rig_hold(&r, false, POLL_MS);
        rig_hold(&r, true, 100);
        rig_hold(&r, false, POLL_MS);
        rig_hold(&r, true, POLL_MS);
        rig_hold(&r, false, 150);
    }
    CHECK(r.doubles == 1); /* 2 physical clicks, not 4 or 6 */
}

static void test_three_clicks_give_one_double_then_start_over(void)
{
    rig_t r;
    rig_init(&r);
    for (int click = 0; click < 3; click++) {
        rig_hold(&r, true, 100);
        rig_hold(&r, false, 150);
    }
    CHECK(r.doubles == 1); /* the 3rd click only starts a new pair */
    rig_hold(&r, true, 100);
    rig_hold(&r, false, 300);
    CHECK(r.doubles == 2); /* 3rd + 4th click = second double */
}

static void test_button_held_at_start_does_not_fire(void)
{
    rig_t r;
    rig_init(&r);
    rig_hold(&r, true, 3000); /* e.g. BOOT held while the board starts */
    rig_hold(&r, false, 500);
    CHECK(r.doubles == 0);
}

static void test_null_state_does_not_crash(void)
{
    button_click_init(NULL);
    CHECK(button_click_feed(NULL, true, 0) == BUTTON_EVT_NONE);
}

int main(void)
{
    test_two_quick_clicks_give_one_double();
    test_single_click_gives_nothing();
    test_clicks_too_far_apart_are_not_a_double();
    test_slow_pair_then_fast_pair_counts_the_fast_one();
    test_long_press_is_not_a_click();
    test_contact_bounce_is_ignored();
    test_three_clicks_give_one_double_then_start_over();
    test_button_held_at_start_does_not_fire();
    test_null_state_does_not_crash();
    printf(s_failures == 0 ? "all button_click tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
