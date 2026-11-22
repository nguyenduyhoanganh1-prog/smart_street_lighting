/**
 * @file test_zone_rule.c
 * @brief Host (PC) test of main/zone_rule.c, no ESP-IDF needed.
 *
 * Build (from firmware/streetlight-node/test/host):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main -I../../../components/road_zones/include \
 *       test_zone_rule.c ../../main/zone_rule.c ../../../components/road_zones/road_zones.c -lm -o test_zone_rule
 */
#include <stdio.h>
#include <string.h>

#include "zone_rule.h"

#define BASE 20U
#define FULL 100U
#define FRAME_W 320U

static int s_failures;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                             \
            s_failures++;                                                                \
        }                                                                                \
    } while (0)

/* A road covering x 20..300, y 50..150 cut in 4 equal image widths: boundaries 90, 160, 230 */
static road_zones_t make_road(void)
{
    road_zones_t road;
    memset(&road, 0, sizeof(road));
    road.valid = true;
    road.top_a = 0.0f;
    road.top_b = 50.0f;
    road.bottom_a = 0.0f;
    road.bottom_b = 150.0f;
    road.x_min = 20;
    road.x_max = 300;
    road.boundaries_x[0] = 90.0f;
    road.boundaries_x[1] = 160.0f;
    road.boundaries_x[2] = 230.0f;
    return road;
}

static unsigned lit_count(const uint8_t duty[ZONE_RULE_LAMPS])
{
    unsigned n = 0;
    for (unsigned k = 0; k < ZONE_RULE_LAMPS; k++) {
        n += (duty[k] == FULL) ? 1U : 0U;
    }
    return n;
}

static void test_nothing_seen_is_base(void)
{
    zone_rule_t rule;
    uint8_t duty[ZONE_RULE_LAMPS];
    zone_rule_reset(&rule);
    zone_rule_duty(&rule, 1000, BASE, FULL, duty);
    for (unsigned k = 0; k < ZONE_RULE_LAMPS; k++) {
        CHECK(duty[k] == BASE);
    }
}

static void test_person_in_middle_zone_lights_three_lamps(void)
{
    zone_rule_t rule;
    zone_rule_stats_t stats;
    uint8_t duty[ZONE_RULE_LAMPS];
    const road_zones_t road = make_road();
    const zone_obj_t person = { .x = 120.0f, .y = 100.0f, .vehicle = false }; /* zone 2 (index 1) */
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, &person, 1, &road, FRAME_W, 1000, &stats);
    zone_rule_duty(&rule, 1000, BASE, FULL, duty);
    CHECK(duty[0] == FULL && duty[1] == FULL && duty[2] == FULL && duty[3] == BASE);
    CHECK(stats.persons == 1 && stats.vehicles == 0);
    CHECK(stats.zone_mask == 0x02U);
}

static void test_edge_zones_do_not_wrap(void)
{
    zone_rule_t rule;
    uint8_t duty[ZONE_RULE_LAMPS];
    const road_zones_t road = make_road();
    const zone_obj_t first = { .x = 40.0f, .y = 100.0f, .vehicle = true };
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, &first, 1, &road, FRAME_W, 0, NULL);
    zone_rule_duty(&rule, 0, BASE, FULL, duty);
    CHECK(duty[0] == FULL && duty[1] == FULL && duty[2] == BASE && duty[3] == BASE);

    zone_rule_reset(&rule);
    const zone_obj_t last = { .x = 280.0f, .y = 100.0f, .vehicle = true };
    zone_rule_feed(&rule, &last, 1, &road, FRAME_W, 0, NULL);
    zone_rule_duty(&rule, 0, BASE, FULL, duty);
    CHECK(duty[0] == BASE && duty[1] == BASE && duty[2] == FULL && duty[3] == FULL);
}

static void test_hold_times_differ_for_person_and_vehicle(void)
{
    zone_rule_t rule;
    uint8_t duty[ZONE_RULE_LAMPS];
    const road_zones_t road = make_road();
    const zone_obj_t person = { .x = 120.0f, .y = 100.0f, .vehicle = false };
    const zone_obj_t car = { .x = 120.0f, .y = 100.0f, .vehicle = true };

    zone_rule_reset(&rule);
    zone_rule_feed(&rule, &car, 1, &road, FRAME_W, 0, NULL);
    zone_rule_duty(&rule, ZONE_RULE_HOLD_VEHICLE_MS - 1, BASE, FULL, duty);
    CHECK(lit_count(duty) == 3U); /* still held */
    zone_rule_duty(&rule, ZONE_RULE_HOLD_VEHICLE_MS, BASE, FULL, duty);
    CHECK(lit_count(duty) == 0U); /* vehicle hold over */

    zone_rule_reset(&rule);
    zone_rule_feed(&rule, &person, 1, &road, FRAME_W, 0, NULL);
    zone_rule_duty(&rule, ZONE_RULE_HOLD_VEHICLE_MS, BASE, FULL, duty);
    CHECK(lit_count(duty) == 3U); /* a person is held longer */
    zone_rule_duty(&rule, ZONE_RULE_HOLD_PERSON_MS, BASE, FULL, duty);
    CHECK(lit_count(duty) == 0U);
}

static void test_object_seen_again_extends_the_hold(void)
{
    zone_rule_t rule;
    uint8_t duty[ZONE_RULE_LAMPS];
    const road_zones_t road = make_road();
    const zone_obj_t car = { .x = 120.0f, .y = 100.0f, .vehicle = true };
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, &car, 1, &road, FRAME_W, 0, NULL);
    zone_rule_feed(&rule, &car, 1, &road, FRAME_W, 1500, NULL);
    zone_rule_duty(&rule, 3000, BASE, FULL, duty); /* 0 + 2000 is over, 1500 + 2000 is not */
    CHECK(lit_count(duty) == 3U);
}

static void test_off_road_objects_are_ignored(void)
{
    zone_rule_t rule;
    zone_rule_stats_t stats;
    uint8_t duty[ZONE_RULE_LAMPS];
    const road_zones_t road = make_road();
    const zone_obj_t objs[] = {
        { .x = 120.0f, .y = 10.0f, .vehicle = false },   /* far above the road */
        { .x = 120.0f, .y = 220.0f, .vehicle = true },   /* far below */
        { .x = 5.0f, .y = 100.0f, .vehicle = false },    /* left of the road */
    };
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, objs, 3, &road, FRAME_W, 0, &stats);
    zone_rule_duty(&rule, 0, BASE, FULL, duty);
    CHECK(lit_count(duty) == 0U);
    CHECK(stats.persons == 0 && stats.vehicles == 0 && stats.zone_mask == 0U);
}

static void test_without_road_uses_quarters_of_the_frame(void)
{
    zone_rule_t rule;
    uint8_t duty[ZONE_RULE_LAMPS];
    const zone_obj_t obj = { .x = 250.0f, .y = 30.0f, .vehicle = false }; /* 250/320 -> quarter 4 (index 3) */
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, &obj, 1, NULL, FRAME_W, 0, NULL);
    zone_rule_duty(&rule, 0, BASE, FULL, duty);
    CHECK(duty[0] == BASE && duty[1] == BASE && duty[2] == FULL && duty[3] == FULL);

    road_zones_t invalid;
    memset(&invalid, 0, sizeof(invalid)); /* valid == false */
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, &obj, 1, &invalid, FRAME_W, 0, NULL);
    zone_rule_duty(&rule, 0, BASE, FULL, duty);
    CHECK(duty[3] == FULL);
}

static void test_zone_mask_counts_objects(void)
{
    zone_rule_t rule;
    zone_rule_stats_t stats;
    const road_zones_t road = make_road();
    const zone_obj_t objs[] = {
        { .x = 50.0f, .y = 100.0f, .vehicle = true },   /* zone 1 */
        { .x = 260.0f, .y = 100.0f, .vehicle = false }, /* zone 4 */
        { .x = 270.0f, .y = 90.0f, .vehicle = false },  /* zone 4 */
    };
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, objs, 3, &road, FRAME_W, 0, &stats);
    CHECK(stats.vehicles == 1 && stats.persons == 2);
    CHECK(stats.zone_mask == 0x09U); /* zones 1 and 4 */
}

static void test_person_and_vehicle_zone_masks_are_separate(void)
{
    zone_rule_t rule;
    zone_rule_stats_t stats;
    const road_zones_t road = make_road();
    const zone_obj_t objs[] = {
        { .x = 50.0f, .y = 100.0f, .vehicle = true },   /* vehicle, zone 1 */
        { .x = 120.0f, .y = 100.0f, .vehicle = false }, /* person, zone 2 */
        { .x = 270.0f, .y = 90.0f, .vehicle = false },  /* person, zone 4 */
        { .x = 260.0f, .y = 100.0f, .vehicle = true },  /* vehicle, zone 4 (same zone as a person) */
        { .x = 120.0f, .y = 10.0f, .vehicle = false },  /* person off the road: in no mask */
    };
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, objs, 5, &road, FRAME_W, 0, &stats);
    CHECK(stats.person_zone_mask == 0x0AU);  /* zones 2 and 4 */
    CHECK(stats.vehicle_zone_mask == 0x09U); /* zones 1 and 4 */
    CHECK(stats.zone_mask == 0x0BU);         /* the union: zones 1, 2 and 4 */
    CHECK(stats.zone_mask == (uint8_t)(stats.person_zone_mask | stats.vehicle_zone_mask));
}

static void test_bad_arguments_do_not_crash(void)
{
    zone_rule_t rule;
    uint8_t duty[ZONE_RULE_LAMPS];
    zone_rule_reset(&rule);
    zone_rule_feed(&rule, NULL, 0, NULL, FRAME_W, 0, NULL);
    zone_rule_feed(NULL, NULL, 0, NULL, FRAME_W, 0, NULL);
    zone_rule_duty(NULL, 0, BASE, FULL, duty);
    zone_rule_duty(&rule, 0, BASE, FULL, NULL);
    zone_rule_duty(&rule, 0, BASE, FULL, duty);
    CHECK(duty[0] == BASE);
}

int main(void)
{
    test_nothing_seen_is_base();
    test_person_in_middle_zone_lights_three_lamps();
    test_edge_zones_do_not_wrap();
    test_hold_times_differ_for_person_and_vehicle();
    test_object_seen_again_extends_the_hold();
    test_off_road_objects_are_ignored();
    test_without_road_uses_quarters_of_the_frame();
    test_zone_mask_counts_objects();
    test_person_and_vehicle_zone_masks_are_separate();
    test_bad_arguments_do_not_crash();
    printf(s_failures == 0 ? "all zone_rule tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
