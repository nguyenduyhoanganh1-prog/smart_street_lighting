/**
 * @file app_config.h
 * @brief Board wiring and timing constants for streetlight-node.
 *
 * Pin/address source: plans/03-so-do-noi-day.md section 7 and
 * reference/ina219-module.md. Check them against the real board before flashing.
 */
#pragma once

#include <stdint.h>

#define APP_LAMP_COUNT 4

/* ---- I2C sensor bus (BH1750 + 4x INA219) ----
 * The camera uses its own SCCB bus (GPIO 4/5) on I2C port 1 by default,
 * so the sensors use I2C port 0. */
#define APP_I2C_PORT       0
#define APP_I2C_SDA_GPIO   41
#define APP_I2C_SCL_GPIO   42
#define APP_I2C_FREQ_HZ    100000U /* 100 kHz: safe for breadboard wiring */

#define APP_BH1750_ADDR    0x23U   /* ADDR pin to GND */
#define APP_INA219_ADDR_1  0x40U   /* no pad bridged */
#define APP_INA219_ADDR_2  0x41U   /* A0 */
#define APP_INA219_ADDR_3  0x44U   /* A1 */
#define APP_INA219_ADDR_4  0x45U   /* A0 + A1 */
#define APP_INA219_SHUNT_MOHM 100U /* "R100" printed on the module */

/* ---- Lamp PWM -> ULN2003 IN1..IN4 (high = lamp on) ---- */
#define APP_LAMP1_GPIO     1
#define APP_LAMP2_GPIO     14
#define APP_LAMP3_GPIO     21
#define APP_LAMP4_GPIO     47
#define APP_PWM_FREQ_HZ    2000U   /* 2 kHz: fast enough for the eye, slow enough for the ULN2003 (Darlington) */
#define APP_LAMP_ACTIVE_LOW 1      /* 1 = invert the PWM pin: 100 % -> pin low. Needed because this
                                    * board's lamps came out reversed (100 % off, 0 % on). Set 0 if
                                    * the wiring is changed so that a high pin lights the lamp. */

/* ---- Lighting rules (plans/01 section 3) ---- */
#define APP_BASE_DIM_PCT        20U
#define APP_FULL_PCT            100U
#define APP_DAY_NIGHT_HOLD_MS   10000U          /* demo value; 5-10 min outdoors */
#define APP_MANUAL_TIMEOUT_MS   (10U * 60U * 1000U)

/* ---- Energy (plans/01 section 4) ---- */
#define APP_ENERGY_SAVE_PERIOD_MS (10U * 60U * 1000U) /* NVS write interval */
#define APP_P100_WINDOW_MS        10000U  /* steady 100 % this long -> measure P100 */
#define APP_P100_CHANGE_RATIO     0.05f   /* only rewrite P100 if it moved > 5 % */

/* ---- Timing ---- */
#define APP_SAMPLE_PERIOD_MS     200U  /* read all sensors + integrate energy */
#define APP_LOG_PERIOD_MS        2000U /* print sensor values to the serial log */
#define APP_TELEMETRY_PERIOD_MS  5000U /* publish state + energy to MQTT */
#define APP_DEVICE_RETRY_MS      5000U /* re-init sensors that stopped answering */
