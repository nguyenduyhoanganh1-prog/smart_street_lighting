/**
 * @file stop_detect.h
 * @brief Stopped-object detection (plans/01 section "Vật dừng lâu"): follows FOMO centres from frame to
 *        frame and reports an object that stayed (almost) still for stop_ms. Pure logic: no ESP-IDF,
 *        unit-tested on the PC (test/host/test_stop_detect.c).
 *
 * Tracking: each detection is matched to the nearest track of the same class that moved at most
 * STOP_DETECT_MATCH_PX since the last frame. A track keeps an "anchor" (where it became still); moving
 * more than eps_px from it restarts the still time. FOMO misses an object now and then, so a track is
 * dropped only after STOP_DETECT_LOST_MS without a detection.
 *
 * One alert at a time (the dashboard shows one): RAISE for the track still the longest, CLEAR when that
 * object leaves (lost) or moves away. After stop_detect_ack() (button "Đã xử lý") the same object is not
 * reported again; a new object, or the same one after it moved, can be.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STOP_DETECT_MAX_TRACKS 8U
#define STOP_DETECT_MATCH_PX   45.0f  /* max move between two frames of one object (~2 FOMO cells) */
#define STOP_DETECT_LOST_MS    3000   /* unseen this long -> the object left */

/** One detection on the road, frame pixels. */
typedef struct {
    float x;
    float y;
    bool vehicle;    /* false = person */
    uint8_t score;   /* % */
    uint8_t zone;    /* 1..4 */
} stop_obj_t;

typedef struct {
    bool used;
    bool alerted;            /* this track raised the open alert */
    bool acked;              /* alert acknowledged: never raised again for this track */
    stop_obj_t last;
    float anchor_x;
    float anchor_y;
    int64_t still_since_ms;
    int64_t last_seen_ms;
} stop_track_t;

typedef struct {
    stop_track_t tracks[STOP_DETECT_MAX_TRACKS];
    bool alert_active;
} stop_detect_t;

typedef enum {
    STOP_EVT_NONE = 0,
    STOP_EVT_RAISE,  /* obj has been still for stop_ms: open the alert */
    STOP_EVT_CLEAR,  /* the alerted object left or moved: close the alert */
} stop_evt_type_t;

typedef struct {
    stop_evt_type_t type;
    stop_obj_t obj;          /* RAISE: the stopped object (last detection) */
    int64_t still_since_ms;  /* RAISE: when it stopped */
} stop_evt_t;

void stop_detect_init(stop_detect_t *sd);

/**
 * Feed the detections of one NEW frame (call once per vision result, not per control cycle).
 * @param eps_px  movement below this from the anchor counts as "still"
 * @return at most one event per call
 */
stop_evt_t stop_detect_update(stop_detect_t *sd, const stop_obj_t *objs, unsigned count, int64_t now_ms,
                              uint32_t stop_ms, float eps_px);

/** The open alert was acknowledged on the dashboard: close it, do not report that object again. */
void stop_detect_ack(stop_detect_t *sd);

/**
 * Since when is the object at (x, y) still? Looks up the nearest followed object of that class within
 * STOP_DETECT_MATCH_PX (acknowledged ones included). @return false if no such object is followed
 */
bool stop_detect_still_since(const stop_detect_t *sd, float x, float y, bool vehicle, int64_t *since_ms);

#ifdef __cplusplus
}
#endif
