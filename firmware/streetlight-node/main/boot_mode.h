/**
 * @file boot_mode.h
 * @brief Which program the board runs after a start: normal node, or ai-stream (camera web page).
 *
 * Switching = remember the wanted mode, then software-restart. The two modes never run together: they
 * need the same camera, the WiFi radio (station + MQTT vs. own access point) and the PSRAM, so a clean
 * restart is simpler and safer than tearing one mode down and building the other in place.
 *
 * The wanted mode lives in RTC RAM (RTC_NOINIT_ATTR): it survives esp_restart() but NOT a power cycle,
 * a brown-out, a panic or a watchdog reset. So any unexpected reset brings the field node back to the
 * normal mode, and no flash is written.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_AI_STREAM,
} app_mode_t;

/** Mode to run now. Call once at the start of app_main(). */
app_mode_t boot_mode_get(void);

/** Toggle normal <-> ai-stream and restart. Does not return. */
void boot_mode_switch(void);

#ifdef __cplusplus
}
#endif
