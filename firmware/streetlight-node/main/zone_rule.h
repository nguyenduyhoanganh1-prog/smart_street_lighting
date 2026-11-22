/**
 * @file zone_rule.h
 * @brief Night lighting rule by zone (plans/01 section 3): which lamps must be bright because a
 *        person or a vehicle is on the road.
 *
 * Pure logic, no ESP-IDF: unit-tested on the PC (test/host).
 *
 *   person at zone K            -> lamps K-1, K, K+1 bright, kept ZONE_RULE_HOLD_PERSON_MS after it left
 *   vehicle at zone K           -> lamps K-1, K, K+1 bright, kept ZONE_RULE_HOLD_VEHICLE_MS
 *                                  (direction tracking is not implemented yet: "direction unknown" row)
 *   object off the road         -> ignored (scaffold, cables, people on the kerb)
 *   everything else             -> base (dim) level
 *
 * Zones come from the road found at start-up (road_zones.h). Without a road, zone k = the k-th
 * quarter of the frame width (the rule used before the calibration existed).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "road_zones.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZONE_RULE_LAMPS            ROAD_ZONES_COUNT
#define ZONE_RULE_HOLD_PERSON_MS   5000
#define ZONE_RULE_HOLD_VEHICLE_MS  2000

/** One detection, centre in frame pixels. */
typedef struct {
    float x;
    float y;
    bool vehicle; /**< false = person */
} zone_obj_t;

/** What was seen in this frame, for the telemetry. */
typedef struct {
    uint16_t persons;    /**< on the road */
    uint16_t vehicles;   /**< on the road */
    uint8_t zone_mask;   /**< bit0 = zone 1 ... bit3 = zone 4: zones holding an object right now (persons | vehicles) */
    uint8_t person_zone_mask;  /**< same bits, only the zones that hold a person */
    uint8_t vehicle_zone_mask; /**< same bits, only the zones that hold a vehicle */
} zone_rule_stats_t;

typedef struct {
    int64_t lit_until_ms[ZONE_RULE_LAMPS]; /**< lamp k stays bright until this time */
} zone_rule_t;

/** All lamps at the base level. */
void zone_rule_reset(zone_rule_t *rule);

/**
 * Feed the detections of one frame. Call once per new frame (not once per control cycle).
 * @param road      calibration result, or NULL / !valid for the quarter-of-the-frame zones
 * @param frame_w   frame width in pixels (for the quarter fallback)
 * @param stats     filled for this frame; may be NULL
 */
void zone_rule_feed(zone_rule_t *rule, const zone_obj_t *objs, unsigned count, const road_zones_t *road,
                    uint16_t frame_w, int64_t now_ms, zone_rule_stats_t *stats);

/** Brightness of every lamp at time now_ms: `full` while its hold runs, else `base`. */
void zone_rule_duty(const zone_rule_t *rule, int64_t now_ms, uint8_t base, uint8_t full,
                    uint8_t duty[ZONE_RULE_LAMPS]);

#ifdef __cplusplus
}
#endif
