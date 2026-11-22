/**
 * @file button_click.h
 * @brief Double-click detector for one push button. Pure logic: no GPIO, no ESP-IDF calls.
 *
 * The caller samples the button every few ms and passes the level and the time. Keeping the timing
 * rules here (and the GPIO in mode_button.c) lets them be unit-tested on a PC.
 *
 * Rules:
 *  - the level must be stable BUTTON_DEBOUNCE_MS before it counts (contact bounce);
 *  - a click is press + release shorter than BUTTON_MAX_CLICK_MS (longer = held, not a click);
 *  - a double click = two clicks whose releases are at most BUTTON_DOUBLE_GAP_MS apart;
 *  - the event is returned on the RELEASE of the second click, never on its press. This matters on
 *    the BOOT button (GPIO0): if the board restarted while GPIO0 is still held low it would enter the
 *    ROM download mode instead of running the program.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_DEBOUNCE_MS    30
#define BUTTON_MAX_CLICK_MS   500
#define BUTTON_DOUBLE_GAP_MS  700

typedef enum {
    BUTTON_EVT_NONE = 0,
    BUTTON_EVT_DOUBLE_CLICK,
} button_evt_t;

typedef struct {
    bool raw_pressed;          /* last sample */
    bool stable_pressed;       /* debounced level */
    bool first_click_pending;  /* one click done, waiting for the second */
    int64_t raw_changed_ms;    /* when the raw level last changed */
    int64_t press_start_ms;    /* when the debounced level went to pressed */
    int64_t first_release_ms;  /* release time of the pending first click */
} button_click_t;

/** Start with the button released. NULL is ignored. */
void button_click_init(button_click_t *state);

/**
 * Feed one sample.
 * @param pressed  true while the button is held down (after any active-low inversion)
 * @param now_ms   monotonic time of the sample
 * @return BUTTON_EVT_DOUBLE_CLICK once per double click, otherwise BUTTON_EVT_NONE
 */
button_evt_t button_click_feed(button_click_t *state, bool pressed, int64_t now_ms);

#ifdef __cplusplus
}
#endif
