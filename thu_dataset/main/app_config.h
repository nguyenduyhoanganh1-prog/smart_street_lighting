/**
 * @file app_config.h
 * @brief Board wiring and capture constants for thu_dataset.
 *
 * Camera pins follow the Freenove ESP32-S3-WROOM CAM (plans/03-so-do-noi-day.md section 7).
 * The SD pins (GPIO 38/39/40, 1-bit SDMMC) are NOT verified against the board schematic yet:
 * check them first if the card does not mount. Lamp wiring is the same as streetlight-wifi.
 */
#pragma once

#include <stdint.h>

/* ---- Frame: 640x360 8-bit gray = the middle 360 rows of a 640x480 (VGA) camera frame ----
 * The camera driver sizes its DMA buffers from the frame size chosen at init, so the sensor is
 * run at plain VGA and the 360 middle rows are kept. Whole rows are contiguous in memory, so the
 * crop is just a pointer offset (no copy). Field of view equals a 16:9 window of the sensor. */
#define APP_CAM_SRC_WIDTH      640U
#define APP_CAM_SRC_HEIGHT     480U
#define APP_FRAME_WIDTH        640U
#define APP_FRAME_HEIGHT       360U
#define APP_BYTES_PER_PIXEL    1U   /* grayscale: the frame buffer keeps only the Y byte */
#define APP_FRAME_BYTES        (APP_FRAME_WIDTH * APP_FRAME_HEIGHT * APP_BYTES_PER_PIXEL) /* 230400 */
#define APP_CROP_ROWS_TOP      ((APP_CAM_SRC_HEIGHT - APP_FRAME_HEIGHT) / 2U)             /* 60 */
#define APP_CROP_OFFSET_BYTES  (APP_CROP_ROWS_TOP * APP_FRAME_WIDTH * APP_BYTES_PER_PIXEL)

/* ---- Camera OV2640 (DVP). Camera SCCB uses its own bus on GPIO 4/5 ---- */
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
#define APP_CAM_XCLK_HZ    20000000

/* ---- SD card, 1-bit SDMMC (unverified pins, see file header) ---- */
#define APP_SD_CLK_GPIO    39
#define APP_SD_CMD_GPIO    38
#define APP_SD_D0_GPIO     40
#define APP_SD_MOUNT_POINT "/sdcard"
#define APP_SD_DATASET_DIR "/sdcard/dataset"

/* ---- Lamps -> ULN2003 IN1..IN4, same wiring as streetlight-wifi/main/app_config.h ---- */
#define APP_LAMP_COUNT      4
#define APP_LAMP1_GPIO      1
#define APP_LAMP2_GPIO      14
#define APP_LAMP3_GPIO      21
#define APP_LAMP4_GPIO      47
#define APP_PWM_FREQ_HZ     2000U
#define APP_LAMP_ACTIVE_LOW 1      /* 1 = 100 % -> pin low (this board's lamps are inverted) */

/* ---- Capture ---- */
#define APP_DEFAULT_PERIOD_S     5U    /* initial auto-capture period shown on the web page */

/* ---- Web preview: the stored frame shrunk by one whole factor (640x360 -> 160x90, every 4x4
 * block becomes one pixel) and sent as a small gray JPEG. Preview only, never stored. ---- */
#define APP_PREVIEW_WIDTH        160U
#define APP_PREVIEW_HEIGHT       90U
#define APP_PREVIEW_JPEG_QUALITY 60    /* 1..100, higher = better */
