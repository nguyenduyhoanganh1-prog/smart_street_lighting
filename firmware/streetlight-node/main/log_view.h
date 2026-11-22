/**
 * @file log_view.h
 * @brief TEMPORARY, for plans/eval.md: pick which log lines the board prints by typing a digit in the
 *        serial monitor (idf.py monitor). 0 = everything (normal), 1..6 = only the lines of that section
 *        of plans/eval.md. Only active when APP_EVAL_LOGS is 1.
 *
 * How: a small task reads the console UART. In a section view the other components are lowered to
 * warnings only, and main.c asks log_view_show() before printing each of its own lines.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sections of plans/eval.md */
typedef enum {
    LOG_ALL = 0,      /**< normal log, every line */
    LOG_AI = 1,       /**< 1. FOMO: fps, detections */
    LOG_ZONE = 2,     /**< 2. zones: road calibration, which zone holds what */
    LOG_CONTROL = 3,  /**< 3. lamp control: cycle time, detection -> lamp latency, duty per lamp */
    LOG_ENERGY = 4,   /**< 4. energy: V/I/P and mWh per lamp */
    LOG_ALERT = 5,    /**< 5. alerts: stopped vehicles, lamp faults, sensor faults */
    LOG_SYSTEM = 6,   /**< 6. system + network: RAM, uptime, MQTT, web latencies */
} log_section_t;

/** Start reading the serial monitor keys (call once from app_main). */
void log_view_start(void);

/** true when lines of `section` must be printed in the current view. */
bool log_view_show(log_section_t section);

#ifdef __cplusplus
}
#endif
