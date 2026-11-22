/**
 * @file mode_button.h
 * @brief BOOT button (GPIO0): a double click switches normal <-> ai-stream (see boot_mode.h).
 *
 * One small task polls the pin and feeds button_click.c. It runs in both modes, so the same double
 * click that enters ai-stream also leaves it. The switch happens on the release of the second click,
 * so GPIO0 is already high when the board restarts (low at reset would start the ROM download mode).
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Configure GPIO0 (input, pull-up) and start the polling task. Call early in app_main(). */
esp_err_t mode_button_start(void);

#ifdef __cplusplus
}
#endif
