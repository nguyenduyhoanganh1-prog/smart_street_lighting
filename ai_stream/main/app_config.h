/**
 * @file app_config.h
 * @brief Board wiring and tuning constants for ai_stream.
 *
 * Camera/lamp pins are the same as thu_dataset (verified on the real board there).
 * Model facts (128x128 gray, labels car/human) come from model-parameters/model_metadata.h and
 * are checked at build time in detector.cpp, so a re-exported model that changes them fails the
 * build instead of silently mis-detecting.
 */
#pragma once

#include <stdint.h>

/* ---- Frame: the whole QVGA camera frame, 8-bit gray (same as the dataset collected for training) ---- */
#define APP_CAM_SRC_WIDTH      320U
#define APP_CAM_SRC_HEIGHT     240U
#define APP_FRAME_WIDTH        320U
#define APP_FRAME_HEIGHT       240U
#define APP_BYTES_PER_PIXEL    1U
#define APP_FRAME_BYTES        (APP_FRAME_WIDTH * APP_FRAME_HEIGHT * APP_BYTES_PER_PIXEL)
#define APP_CROP_ROWS_TOP      ((APP_CAM_SRC_HEIGHT - APP_FRAME_HEIGHT) / 2U) /* 0: nothing cropped */
#define APP_CROP_OFFSET_BYTES  (APP_CROP_ROWS_TOP * APP_FRAME_WIDTH * APP_BYTES_PER_PIXEL)

/* ---- Camera OV2640 (DVP), SCCB on its own bus GPIO 4/5 ---- */
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

/* ---- Lamps -> ULN2003 IN1..IN4, same wiring as streetlight-wifi ---- */
#define APP_LAMP_COUNT      4
#define APP_LAMP1_GPIO      47
#define APP_LAMP2_GPIO      21
#define APP_LAMP3_GPIO      20   /* USB D+ pin: LEDC turns the native USB port off, flash via the UART (COM) port */
#define APP_LAMP4_GPIO      19   /* USB D- pin: same note as lamp 3 */
#define APP_PWM_FREQ_HZ     2000U
#define APP_LAMP_ACTIVE_LOW 1      /* 1 = 100 % -> pin low (this board's lamps are inverted) */

/* ---- Stream: the 320x240 gray frame sent as JPEG; detections are drawn by the page ---- */
#define APP_STREAM_JPEG_QUALITY 50    /* 1..100 */
#define APP_STREAM_JPEG_BYTES   (24U * 1024U)

/* ---- Detection -> lamp rule (plans/01 section 3, simplified: no direction/tracking yet) ----
 * Zone k (0..3) = the k-th of 4 equal stretches of the road found at start-up (road_zones.c,
 * method D), lamp 1 = leftmost zone in the image. Objects off the road are ignored. If the road
 * is not found, zone k = the k-th quarter of the frame width (the old rule).
 * Any object in zone k lights k-1, k, k+1. */
#define APP_ZONE_COUNT       APP_LAMP_COUNT
#define APP_AI_BASE_DIM_PCT  20U      /* lamps with nothing near them (night base level) */
#define APP_AI_ACTIVE_PCT    100U
#define APP_AI_HOLD_MS       2000U    /* keep a zone lit this long after the object left */
#define APP_AI_FADE_UP_MS    200U
#define APP_AI_FADE_DOWN_MS  1000U

/* ---- Start-up road calibration (road_zones.c): every lamp at this level, wait, take one frame ---- */
#define APP_CALIB_LAMP_PCT      100U
#define APP_CALIB_SETTLE_MS     1500U  /* camera auto exposure adapts to the new light */
#define APP_CALIB_SKIP_FRAMES   3U     /* frames still exposed before the lamps changed are dropped */
