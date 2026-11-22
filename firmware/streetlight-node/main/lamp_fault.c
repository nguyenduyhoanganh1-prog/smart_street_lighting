/**
 * @file lamp_fault.c
 * @brief Lamp fault detection (see lamp_fault.h).
 */
#include "lamp_fault.h"

#include <math.h>

void lamp_fault_init(lamp_fault_state_t *st)
{
    st->reported = LAMP_FAULT_NONE;
    st->pending = LAMP_FAULT_NONE;
    st->pending_since_ms = -1;
    st->steady_since_ms = -1;
}

/* Lamp on but drawing far less than it should. fabsf: a channel with Vin+/Vin- swapped reads a
 * negative current, which still proves the lamp is lit. */
static bool too_little(const lamp_fault_in_t *in)
{
    if (in->p100_mw > 0.0f) {
        const float expected_mw = in->p100_mw * (float)in->duty / 100.0f;
        return fabsf(in->power_mw) < LAMP_FAULT_OPEN_RATIO * expected_mw;
    }
    return fabsf(in->current_ma) < LAMP_FAULT_OPEN_MIN_MA;
}

/* Verdict of one sample; false = this sample cannot tell (fading, or a too dim lamp) */
static bool judge(const lamp_fault_in_t *in, bool settled, lamp_fault_t last, lamp_fault_t *out)
{
    if (!in->ina_ok) {
        *out = LAMP_FAULT_SENSOR;
        return true;
    }
    if (in->bus_v < LAMP_FAULT_LOW_V) {
        *out = LAMP_FAULT_NO_SUPPLY;
        return true;
    }
    if (fabsf(in->current_ma) > LAMP_FAULT_SHORT_MA) {
        *out = LAMP_FAULT_SHORT;
        return true;
    }
    if (!settled) {
        return false; /* mid-fade the reading does not match the duty */
    }
    if (in->duty == 0U) {
        if (fabsf(in->current_ma) > LAMP_FAULT_STUCK_MA) {
            *out = LAMP_FAULT_STUCK_ON;
            return true;
        }
        /* an open lamp and a healthy one both read 0 when off: keep "open" until it is lit again */
        if (last == LAMP_FAULT_OPEN) {
            return false;
        }
        *out = LAMP_FAULT_NONE;
        return true;
    }
    if (in->duty < LAMP_FAULT_MIN_DUTY) {
        return false;
    }
    *out = too_little(in) ? LAMP_FAULT_OPEN : LAMP_FAULT_NONE;
    return true;
}

lamp_fault_t lamp_fault_update(lamp_fault_state_t *st, const lamp_fault_in_t *in)
{
    if (!in->steady) {
        st->steady_since_ms = -1;
    } else if (st->steady_since_ms < 0) {
        st->steady_since_ms = in->now_ms;
    }
    const bool settled = st->steady_since_ms >= 0
                         && in->now_ms - st->steady_since_ms >= (int64_t)LAMP_FAULT_SETTLE_MS;

    lamp_fault_t verdict;
    if (!judge(in, settled, st->reported, &verdict)) {
        return st->reported; /* no new information: the running confirmation is kept as it is */
    }
    if (verdict == st->reported) {
        st->pending = verdict; /* one sample agreeing with the report breaks any confirmation */
        st->pending_since_ms = -1;
        return st->reported;
    }
    if (verdict != st->pending || st->pending_since_ms < 0) {
        st->pending = verdict;
        st->pending_since_ms = in->now_ms;
    }
    const int64_t hold_ms = verdict == LAMP_FAULT_NONE ? LAMP_FAULT_CLEAR_MS : LAMP_FAULT_RAISE_MS;
    if (in->now_ms - st->pending_since_ms >= hold_ms) {
        st->reported = verdict;
        st->pending_since_ms = -1;
    }
    return st->reported;
}
