/**
 * @file lamp_ctrl.h
 * @brief The 4 lamps: manual levels from the web page, or automatic from the detections.
 *
 * Automatic rule (simplified from plans/01 section 3, no direction/tracking yet): an object in
 * zone k lights lamps k-1, k and k+1 at full level and keeps them lit APP_AI_HOLD_MS after it
 * left; every other lamp stays at the base level. Zones come from the start-up road calibration
 * (lamp_ctrl_set_road); objects off the road are ignored. Without a road, zone k = the k-th
 * quarter of the frame width. Setting a lamp by hand switches the automatic mode off.
 * Thread-safe (mutex inside).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "detector.h"
#include "esp_err.h"
#include "road_zones.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the LEDC PWM; all lamps off, automatic mode off. */
esp_err_t lamp_ctrl_init(void);

/**
 * Set one lamp by hand (instant, no fade: the next camera frame must match the level) and
 * switch the automatic mode off.
 * @param lamp  0 = all lamps, 1..APP_LAMP_COUNT = one lamp
 * @param pct   0..100
 */
esp_err_t lamp_ctrl_set_manual(unsigned lamp, uint8_t pct);

/** Switch the automatic mode. Turning it on starts every lamp at the base level. */
void lamp_ctrl_set_auto(bool enable);

/**
 * Call once per camera cycle. Feeds the latest detections to the automatic rule (det may be
 * NULL when there is no new result) and services the PWM fades.
 */
void lamp_ctrl_update(const detection_result_t *det, int64_t now_ms);

/**
 * Use the road found at start-up for the zones (a copy is kept). A road with valid == false
 * brings back the quarter-of-the-frame zones.
 */
void lamp_ctrl_set_road(const road_zones_t *road);

/** Current levels (0..100 %) and the automatic mode. */
void lamp_ctrl_get(uint8_t pct[APP_LAMP_COUNT], bool *auto_on);

#ifdef __cplusplus
}
#endif
