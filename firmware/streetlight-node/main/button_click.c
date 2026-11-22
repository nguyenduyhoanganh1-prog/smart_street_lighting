/**
 * @file button_click.c
 * @brief Double-click detector (see button_click.h).
 */
#include "button_click.h"

#include <string.h>

void button_click_init(button_click_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
}

/* A debounced release: decide whether it completes a double click */
static button_evt_t on_release(button_click_t *state, int64_t now_ms)
{
    if (now_ms - state->press_start_ms > BUTTON_MAX_CLICK_MS) {
        state->first_click_pending = false; /* held too long: not a click, forget the first one too */
        return BUTTON_EVT_NONE;
    }
    if (state->first_click_pending && now_ms - state->first_release_ms <= BUTTON_DOUBLE_GAP_MS) {
        state->first_click_pending = false;
        return BUTTON_EVT_DOUBLE_CLICK;
    }
    /* First click, or the previous one is too old: this click starts a new pair */
    state->first_click_pending = true;
    state->first_release_ms = now_ms;
    return BUTTON_EVT_NONE;
}

button_evt_t button_click_feed(button_click_t *state, bool pressed, int64_t now_ms)
{
    if (state == NULL) {
        return BUTTON_EVT_NONE;
    }
    if (pressed != state->raw_pressed) {
        state->raw_pressed = pressed; /* the level moved: restart the stability timer */
        state->raw_changed_ms = now_ms;
        return BUTTON_EVT_NONE;
    }
    if (pressed == state->stable_pressed || now_ms - state->raw_changed_ms < BUTTON_DEBOUNCE_MS) {
        return BUTTON_EVT_NONE;
    }
    state->stable_pressed = pressed;
    if (pressed) {
        state->press_start_ms = now_ms;
        return BUTTON_EVT_NONE;
    }
    return on_release(state, now_ms);
}
