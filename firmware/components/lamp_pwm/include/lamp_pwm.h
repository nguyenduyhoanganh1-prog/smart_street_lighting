/**
 * @file lamp_pwm.h
 * @brief Lamp dimming with LEDC hardware fades (ESP32-S3, 4 channels).
 *
 * Fade rules (plans/01 section 3):
 *   - brighter: stop any running fade and fade up from the current level (fast)
 *   - darker:   fade down slowly; if a fade-up is still running, wait for it
 *               to finish first (call lamp_pwm_service() periodically)
 *
 * Resources: LEDC timer 1, channels 2..5. Timer 0 / channel 0 are left free
 * for esp32-camera, which uses them to generate the camera XCLK.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAMP_PWM_MAX_CHANNELS 4

/**
 * Configure the LEDC timer and one channel per GPIO; all lamps start off.
 * @param active_low  true when the lamp lights while the pin is LOW (output inverted by the
 *                    LEDC hardware, so percentages keep their meaning: 100 % = brightest).
 *                    Before this call the pins are floating/high, so an inverted lamp may
 *                    flash on for a moment at boot.
 */
esp_err_t lamp_pwm_init(const int gpios[], unsigned count, uint32_t freq_hz, bool active_low);

/**
 * Request a brightness. Repeated calls with the same value are cheap no-ops,
 * so this can be called every control cycle.
 * @param percent  0..100 (clamped)
 */
esp_err_t lamp_pwm_set(unsigned lamp, uint8_t percent, uint32_t fade_up_ms, uint32_t fade_down_ms);

/** Start deferred fade-downs whose preceding fade-up has finished. */
void lamp_pwm_service(void);

/** Brightness right now (follows the running fade), 0..100 %. */
uint8_t lamp_pwm_get(unsigned lamp);

/** Last requested brightness, 0..100 %. */
uint8_t lamp_pwm_target(unsigned lamp);

/** true when no fade is running or pending (output equals target). */
bool lamp_pwm_is_steady(unsigned lamp);

#ifdef __cplusplus
}
#endif
