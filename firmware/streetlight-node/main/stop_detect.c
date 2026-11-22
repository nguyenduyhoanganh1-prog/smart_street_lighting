/**
 * @file stop_detect.c
 * @brief Stopped-object detection (see stop_detect.h).
 */
#include "stop_detect.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static float distance(float ax, float ay, float bx, float by)
{
    return hypotf(ax - bx, ay - by);
}

void stop_detect_init(stop_detect_t *sd)
{
    if (sd != NULL) {
        memset(sd, 0, sizeof(*sd));
    }
}

/* Nearest free-to-match track of the same class within STOP_DETECT_MATCH_PX, -1 if none */
static int match_track(const stop_detect_t *sd, const bool matched[], const stop_obj_t *o)
{
    int best = -1;
    float best_d = STOP_DETECT_MATCH_PX;
    for (unsigned k = 0; k < STOP_DETECT_MAX_TRACKS; k++) {
        const stop_track_t *t = &sd->tracks[k];
        if (!t->used || matched[k] || t->last.vehicle != o->vehicle) {
            continue;
        }
        const float d = distance(t->last.x, t->last.y, o->x, o->y);
        if (d <= best_d) {
            best_d = d;
            best = (int)k;
        }
    }
    return best;
}

static void start_track(stop_track_t *t, const stop_obj_t *o, int64_t now_ms)
{
    memset(t, 0, sizeof(*t));
    t->used = true;
    t->last = *o;
    t->anchor_x = o->x;
    t->anchor_y = o->y;
    t->still_since_ms = now_ms;
    t->last_seen_ms = now_ms;
}

/* Follow one detection; returns true if it ended the alerted object's stop (it moved away) */
static bool follow(stop_detect_t *sd, bool matched[], const stop_obj_t *o, int64_t now_ms, float eps_px)
{
    const int k = match_track(sd, matched, o);
    if (k < 0) {
        for (unsigned f = 0; f < STOP_DETECT_MAX_TRACKS; f++) {
            if (!sd->tracks[f].used) {
                start_track(&sd->tracks[f], o, now_ms);
                matched[f] = true;
                break;
            }
        }
        return false; /* no free slot: this object is not followed */
    }
    stop_track_t *t = &sd->tracks[k];
    matched[k] = true;
    t->last = *o;
    t->last_seen_ms = now_ms;
    if (distance(t->anchor_x, t->anchor_y, o->x, o->y) <= eps_px) {
        return false; /* still */
    }
    /* moved: the still time starts again from here, and an acknowledged stop is over */
    const bool ended = t->alerted;
    t->anchor_x = o->x;
    t->anchor_y = o->y;
    t->still_since_ms = now_ms;
    t->alerted = false;
    t->acked = false;
    return ended;
}

stop_evt_t stop_detect_update(stop_detect_t *sd, const stop_obj_t *objs, unsigned count, int64_t now_ms,
                              uint32_t stop_ms, float eps_px)
{
    stop_evt_t evt = { .type = STOP_EVT_NONE };
    if (sd == NULL || (objs == NULL && count > 0U)) {
        return evt;
    }
    bool matched[STOP_DETECT_MAX_TRACKS] = { false };
    bool alerted_ended = false;
    for (unsigned i = 0; i < count; i++) {
        alerted_ended |= follow(sd, matched, &objs[i], now_ms, eps_px);
    }
    for (unsigned k = 0; k < STOP_DETECT_MAX_TRACKS; k++) {
        stop_track_t *t = &sd->tracks[k];
        if (t->used && !matched[k] && now_ms - t->last_seen_ms > (int64_t)STOP_DETECT_LOST_MS) {
            alerted_ended |= t->alerted; /* the alerted object left */
            t->used = false;
        }
    }

    if (sd->alert_active && alerted_ended) {
        sd->alert_active = false;
        evt.type = STOP_EVT_CLEAR;
        return evt;
    }
    if (sd->alert_active) {
        return evt;
    }
    /* No open alert: report the object still for the longest time, if long enough */
    int best = -1;
    for (unsigned k = 0; k < STOP_DETECT_MAX_TRACKS; k++) {
        const stop_track_t *t = &sd->tracks[k];
        if (!t->used || t->acked || now_ms - t->still_since_ms < (int64_t)stop_ms) {
            continue;
        }
        if (best < 0 || t->still_since_ms < sd->tracks[best].still_since_ms) {
            best = (int)k;
        }
    }
    if (best >= 0) {
        stop_track_t *t = &sd->tracks[best];
        t->alerted = true;
        sd->alert_active = true;
        evt.type = STOP_EVT_RAISE;
        evt.obj = t->last;
        evt.still_since_ms = t->still_since_ms;
    }
    return evt;
}

bool stop_detect_still_since(const stop_detect_t *sd, float x, float y, bool vehicle, int64_t *since_ms)
{
    if (sd == NULL || since_ms == NULL) {
        return false;
    }
    const bool none_matched[STOP_DETECT_MAX_TRACKS] = { false };
    const stop_obj_t probe = { .x = x, .y = y, .vehicle = vehicle };
    const int k = match_track(sd, none_matched, &probe);
    if (k < 0) {
        return false;
    }
    *since_ms = sd->tracks[k].still_since_ms;
    return true;
}

void stop_detect_ack(stop_detect_t *sd)
{
    if (sd == NULL) {
        return;
    }
    sd->alert_active = false;
    for (unsigned k = 0; k < STOP_DETECT_MAX_TRACKS; k++) {
        stop_track_t *t = &sd->tracks[k];
        if (t->alerted) {
            t->alerted = false;
            t->acked = true;
        }
    }
}
