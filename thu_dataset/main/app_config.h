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

/* ---- Frame: 320x240 8-bit gray = the whole QVGA camera frame (4:3, full field of view) ----
 * The sensor scales its full window down to QVGA itself, so nothing is cropped. The crop
 * constants stay so a smaller stored frame (e.g. the middle rows only) is a one-line change:
 * whole rows are contiguous in memory, so a row crop is just a pointer offset (no copy).
 * QVGA also moves 4x less data than VGA, which keeps the camera copy task well ahead of the DMA. */
#define APP_CAM_SRC_WIDTH      320U
#define APP_CAM_SRC_HEIGHT     240U
#define APP_FRAME_WIDTH        320U
#define APP_FRAME_HEIGHT       240U
#define APP_BYTES_PER_PIXEL    1U   /* grayscale: the frame buffer keeps only the Y byte */
#define APP_FRAME_BYTES        (APP_FRAME_WIDTH * APP_FRAME_HEIGHT * APP_BYTES_PER_PIXEL) /* 76800 */
#define APP_CROP_ROWS_TOP      ((APP_CAM_SRC_HEIGHT - APP_FRAME_HEIGHT) / 2U)             /* 0 */
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
/* 10 MHz, not 20: in grayscale mode the OV2640 still sends YUV (2 bytes/pixel) and the driver
 * keeps the Y byte with a CPU copy loop into PSRAM. At 20 MHz that copy fell behind the DMA,
 * the driver's event queue overflowed ("cam_hal: EV-VSYNC-OVF") and no frame ever completed. */
#define APP_CAM_XCLK_HZ    10000000

/* ---- SD card, 1-bit SDMMC (unverified pins, see file header) ---- */
#define APP_SD_CLK_GPIO    39
#define APP_SD_CMD_GPIO    38
#define APP_SD_D0_GPIO     40
#define APP_SD_MOUNT_POINT "/sdcard"
#define APP_SD_DATASET_DIR "/sdcard/dataset"

/* ---- Lamps -> ULN2003 IN1..IN4, same wiring as streetlight-wifi/main/app_config.h ---- */
#define APP_LAMP_COUNT      4
#define APP_LAMP1_GPIO      19   /* USB D- pin: LEDC turns the native USB port off, flash via the UART (COM) port */
#define APP_LAMP2_GPIO      20   /* USB D+ pin: same note as lamp 1 */
#define APP_LAMP3_GPIO      21
#define APP_LAMP4_GPIO      47
#define APP_PWM_FREQ_HZ     2000U
#define APP_LAMP_ACTIVE_LOW 1      /* 1 = 100 % -> pin low (this board's lamps are inverted) */

/* ---- Capture ---- */
#define APP_DEFAULT_PERIOD_S     5U    /* initial auto-capture period shown on the web page */

/* ---- Web preview: the stored frame shrunk by one whole factor (320x240 -> 160x120, every 2x2
 * block becomes one pixel) and sent as a small gray JPEG. Preview only, never stored. ---- */
#define APP_PREVIEW_WIDTH        160U
#define APP_PREVIEW_HEIGHT       120U
#define APP_PREVIEW_JPEG_QUALITY 60    /* 1..100, higher = better */
