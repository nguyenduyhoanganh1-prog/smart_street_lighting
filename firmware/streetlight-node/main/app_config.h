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
#define APP_I2C_SDA_GPIO   40      /* real wiring (hdr 33); schematic shows SDA on 40, swapped on the board */
#define APP_I2C_SCL_GPIO   39      /* real wiring (hdr 34). 39/40 are also SD-card pins: no SD card inserted */
#define APP_I2C_FREQ_HZ    100000U /* 100 kHz: safe for breadboard wiring */

#define APP_BH1750_ADDR    0x23U   /* ADDR pin to GND */
#define APP_INA219_ADDR_1  0x40U   /* no pad bridged */
#define APP_INA219_ADDR_2  0x41U   /* A0 */
#define APP_INA219_ADDR_3  0x44U   /* A1 */
#define APP_INA219_ADDR_4  0x45U   /* A0 + A1 */
#define APP_INA219_SHUNT_MOHM 100U /* "R100" printed on the module */

/* ---- Lamp PWM -> ULN2003 IN1..IN4 ----
 * Same order and polarity as ai_stream (checked on the real board): lamp 1 is the leftmost zone in
 * the camera image, lamp 4 the rightmost. streetlight-wifi / thu_dataset still use 19, 20, 21, 47. */
#define APP_LAMP1_GPIO     47
#define APP_LAMP2_GPIO     21
#define APP_LAMP3_GPIO     20   /* USB D+ pin: LEDC turns the native USB port off, flash via the UART (COM) port */
#define APP_LAMP4_GPIO     19   /* USB D- pin: same note as lamp 3 */
#define APP_PWM_FREQ_HZ    2000U   /* 2 kHz: the ULN2003 (Darlington) switches slowly; INA219 averages 340 periods */
#define APP_LAMP_ACTIVE_LOW 1      /* 1 = 100 % -> pin low (this board's lamps are inverted) */

/* ---- Lighting rules (plans/01 section 3) ---- */
#define APP_BASE_DIM_PCT        20U  /* ai-stream mode only; normal mode uses config.base_pct (dashboard) */
#define APP_FULL_PCT            100U
#define APP_DAY_NIGHT_HOLD_MS   10000U          /* demo value; 5-10 min outdoors */
/* BH1750 "dark" this long while the clock says day -> covered/stuck sensor: alert (lux_check.h).
 * Lamps stay off by day in the meantime (lux_check_resolve_night). To demo "night" by day, use force night. */
#define APP_LUX_SUSPECT_MS      (1U * 60U * 1000U)
#define APP_LUX_RECOVER_MS      APP_DAY_NIGHT_HOLD_MS /* bright again this long -> sensor trusted again */
/* Stopped-object alert (stop_detect.h): on/off and time are in the config (dashboard). One FOMO cell is
 * 1/16 of the frame (~20 px); the centre jitters by about one cell, so "still" allows 1.5 cells. */
#define APP_STOP_EPS_PX         30.0f
#define APP_MANUAL_TIMEOUT_MS   (10U * 60U * 1000U)

/* ---- Energy (plans/01 section 4) ---- */
#define APP_ENERGY_SAVE_PERIOD_MS (10U * 60U * 1000U) /* NVS write interval */
#define APP_P100_WINDOW_MS        10000U  /* steady 100 % this long -> measure P100 */
#define APP_P100_CHANGE_RATIO     0.05f   /* only rewrite P100 if it moved > 5 % */

/* ---- Timing ---- */
#define APP_SAMPLE_PERIOD_MS     200U  /* read all sensors + integrate energy */
#define APP_LOG_PERIOD_MS        2000U /* print sensor values to the serial log */
/* TEMPORARY, for plans/eval.md only: latency lines ("latency:", "cmd latency:"), the once-a-minute
 * "health:" line and "MQTT back after". Set to 0 once the evaluation is done. */
#define APP_EVAL_LOGS            1
#define APP_TELEMETRY_PERIOD_MS  5000U /* publish state + energy to MQTT */
#define APP_DEVICE_RETRY_MS      5000U /* re-init sensors that stopped answering */

/* ---- Camera OV2640 (DVP), SCCB on its own bus GPIO 4/5 (same pins and frame as ai_stream) ---- */
#define APP_CAM_SRC_WIDTH      320U
#define APP_CAM_SRC_HEIGHT     240U
#define APP_FRAME_WIDTH        320U   /* the whole QVGA frame, 8-bit gray, like the training dataset */
#define APP_FRAME_HEIGHT       240U
#define APP_BYTES_PER_PIXEL    1U
#define APP_FRAME_BYTES        (APP_FRAME_WIDTH * APP_FRAME_HEIGHT * APP_BYTES_PER_PIXEL)
#define APP_CROP_ROWS_TOP      ((APP_CAM_SRC_HEIGHT - APP_FRAME_HEIGHT) / 2U) /* 0: nothing cropped */
#define APP_CROP_OFFSET_BYTES  (APP_CROP_ROWS_TOP * APP_FRAME_WIDTH * APP_BYTES_PER_PIXEL)

#define APP_CAM_PIN_PWDN   (-1)
#define APP_CAM_PIN_RESET  (-1)
#define APP_CAM_PIN_XCLK   15
#define APP_CAM_PIN_SDA    4
#define APP_CAM_PIN_SCL    5
#define APP_CAM_PIN_D0     11  /* Y2 */
#define APP_CAM_PIN_D1     9   /* Y3 */
#define APP_CAM_PIN_D2     8   /* Y4 */
#define APP_CAM_PIN_D3     10  /* Y5 */
#define APP_CAM_PIN_D4     12  /* Y6 */
#define APP_CAM_PIN_D5     18  /* Y7 */
#define APP_CAM_PIN_D6     17  /* Y8 */
#define APP_CAM_PIN_D7     16  /* Y9 */
#define APP_CAM_PIN_VSYNC  6
#define APP_CAM_PIN_HREF   7
#define APP_CAM_PIN_PCLK   13
/* 10 MHz, not 20: see thu_dataset (EV-VSYNC-OVF at 20 MHz in gray mode) */
#define APP_CAM_XCLK_HZ    10000000

/* ---- Vision (plans/01 sections 2-3): FOMO runs in its own task, the control loop only reads its mailbox ---- */
#define APP_VISION_TIMEOUT_MS      3000U            /* night: no result this long -> camera/model fault -> time-based mode */
#define APP_CAMERA_CHECK_PERIOD_MS (5U * 60U * 1000U) /* day: self-check of the camera, so a fault is known before dark */

/* ---- Road calibration at the start of each night (road_zones.h): every lamp at full level, wait, one frame ---- */
#define APP_CALIB_SETTLE_MS     1500U  /* camera auto exposure adapts to the new light */
#define APP_CALIB_SKIP_FRAMES   3U     /* frames still exposed before the lamps changed are dropped */
#define APP_CALIB_MAX_ATTEMPTS  3U     /* per night; then the zones fall back to quarters of the frame */
#define APP_CALIB_RETRY_MS      5000U  /* pause between two attempts */
#define APP_CALIB_JPEG_QUALITY  50     /* every attempt's frame goes to the dashboard (streetlight/calib/image) */
#define APP_CALIB_JPEG_BYTES    (24U * 1024U) /* < SL_MQTT_IMAGE_MAX_JPEG; a gray 320x240 at 50 is ~10-15 KB */

/* ---- Mode button: double click on the BOOT button switches normal <-> ai-stream (mode_button.c) ---- */
#define APP_MODE_BUTTON_GPIO    0      /* BOOT button, active low. Never hold it while the board resets */
#define APP_MODE_BUTTON_POLL_MS 10U

/* ---- ai-stream mode (ported from ai_stream): own WiFi AP + web page with the live frame, FOMO boxes, lamp sliders ---- */
#define APP_STREAM_JPEG_QUALITY 50                  /* 1..100 */
#define APP_STREAM_JPEG_BYTES   (24U * 1024U)
#define APP_ZONE_COUNT          APP_LAMP_COUNT      /* one road zone per lamp */
#define APP_AI_BASE_DIM_PCT     APP_BASE_DIM_PCT    /* lamps with nothing near them */
#define APP_AI_ACTIVE_PCT       APP_FULL_PCT
#define APP_AI_HOLD_MS          2000U               /* keep a zone lit this long after the object left */
#define APP_AI_FADE_UP_MS       200U
#define APP_AI_FADE_DOWN_MS     1000U
#define APP_CALIB_LAMP_PCT      APP_FULL_PCT        /* start-up road calibration: every lamp at this level */
