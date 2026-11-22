/**
 * @file zone_rule.c
 * @brief Night lighting rule by zone (see zone_rule.h).
 */
#include "zone_rule.h"

#include <stddef.h>
#include <string.h>

/** Zone (0..ZONE_RULE_LAMPS-1) of an object, or -1 when it is off the road. */
static int zone_of_object(const zone_obj_t *obj, const road_zones_t *road, uint16_t frame_w)
{
    if (road != NULL && road->valid) {
        if (!road_zones_contains(road, obj->x, obj->y)) {
            return -1;
        }
        return (int)road_zones_zone_of(road, obj->x);
    }
    if (frame_w == 0U || obj->x < 0.0f) {
        return -1;
    }
    const int zone = (int)(obj->x * (float)ZONE_RULE_LAMPS / (float)frame_w);
    return (zone < (int)ZONE_RULE_LAMPS) ? zone : (int)ZONE_RULE_LAMPS - 1;
}

/** Keep lamps zone-1 .. zone+1 bright until `until_ms` (a later time never shortens a hold). */
static void light_around(zone_rule_t *rule, int zone, int64_t until_ms)
{
    const int first = (zone > 0) ? zone - 1 : 0;
    const int last = (zone + 1 < (int)ZONE_RULE_LAMPS) ? zone + 1 : (int)ZONE_RULE_LAMPS - 1;
    for (int k = first; k <= last; k++) {
        if (until_ms > rule->lit_until_ms[k]) {
            rule->lit_until_ms[k] = until_ms;
        }
    }
}

void zone_rule_reset(zone_rule_t *rule)
{
    if (rule != NULL) {
        memset(rule, 0, sizeof(*rule)); /* 0 ms is "long ago": everything at the base level */
    }
}

void zone_rule_feed(zone_rule_t *rule, const zone_obj_t *objs, unsigned count, const road_zones_t *road,
                    uint16_t frame_w, int64_t now_ms, zone_rule_stats_t *stats)
{
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
    if (rule == NULL || objs == NULL) {
        return;
    }
    for (unsigned i = 0; i < count; i++) {
        const int zone = zone_of_object(&objs[i], road, frame_w);
        if (zone < 0) {
            continue;
        }
        const int64_t hold = objs[i].vehicle ? ZONE_RULE_HOLD_VEHICLE_MS : ZONE_RULE_HOLD_PERSON_MS;
        light_around(rule, zone, now_ms + hold);
        if (stats != NULL) {
            const uint8_t zone_bit = (uint8_t)(1U << (unsigned)zone);
            stats->persons = (uint16_t)(stats->persons + (objs[i].vehicle ? 0U : 1U));
            stats->vehicles = (uint16_t)(stats->vehicles + (objs[i].vehicle ? 1U : 0U));
            if (objs[i].vehicle) {
                stats->vehicle_zone_mask = (uint8_t)(stats->vehicle_zone_mask | zone_bit);
            } else {
                stats->person_zone_mask = (uint8_t)(stats->person_zone_mask | zone_bit);
            }
            stats->zone_mask = (uint8_t)(stats->zone_mask | zone_bit);
        }
    }
}

void zone_rule_duty(const zone_rule_t *rule, int64_t now_ms, uint8_t base, uint8_t full,
                    uint8_t duty[ZONE_RULE_LAMPS])
{
    if (duty == NULL) {
        return;
    }
    for (unsigned k = 0; k < ZONE_RULE_LAMPS; k++) {
        const bool lit = rule != NULL && now_ms < rule->lit_until_ms[k];
        duty[k] = lit ? full : base;
    }
}
