/**
 * @file camera_cap.c
 * @brief OV2640 camera in 8-bit grayscale, one owner at a time (see camera_cap.h).
 */
#include "camera_cap.h"

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "camera_cap"

#define EXPOSURE_LOCK_TIMEOUT_MS 2000U
#define SRC_FRAME_BYTES          (APP_CAM_SRC_WIDTH * APP_CAM_SRC_HEIGHT * APP_BYTES_PER_PIXEL)

static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static bool frame_is_valid(const camera_fb_t *fb)
{
    return fb != NULL && fb->buf != NULL && fb->width == APP_CAM_SRC_WIDTH
           && fb->height == APP_CAM_SRC_HEIGHT && fb->len == SRC_FRAME_BYTES;
}

esp_err_t camera_cap_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);

    const camera_config_t cfg = {
        .pin_pwdn = APP_CAM_PIN_PWDN,
        .pin_reset = APP_CAM_PIN_RESET,
        .pin_xclk = APP_CAM_PIN_XCLK,
        .pin_sccb_sda = APP_CAM_PIN_SDA,
        .pin_sccb_scl = APP_CAM_PIN_SCL,
        .pin_d7 = APP_CAM_PIN_D7,
        .pin_d6 = APP_CAM_PIN_D6,
        .pin_d5 = APP_CAM_PIN_D5,
        .pin_d4 = APP_CAM_PIN_D4,
        .pin_d3 = APP_CAM_PIN_D3,
        .pin_d2 = APP_CAM_PIN_D2,
        .pin_d1 = APP_CAM_PIN_D1,
        .pin_d0 = APP_CAM_PIN_D0,
        .pin_vsync = APP_CAM_PIN_VSYNC,
        .pin_href = APP_CAM_PIN_HREF,
        .pin_pclk = APP_CAM_PIN_PCLK,
        .xclk_freq_hz = APP_CAM_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_0,     /* timer 0 / channel 0 are reserved for the camera clock */
        .ledc_channel = LEDC_CHANNEL_0, /* (lamp_pwm uses timer 1, channels 2..5) */
        .pixel_format = PIXFORMAT_GRAYSCALE, /* the sensor sends YUV, the driver keeps the Y byte only */
        .frame_size = FRAMESIZE_VGA,
        .jpeg_quality = 12,             /* unused for raw gray */
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };
    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s (check the APP_CAM_PIN_* wiring)", esp_err_to_name(err));
        return err;
    }

    /* Prove the whole path works before declaring success: one real frame of the right size */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!frame_is_valid(fb)) {
        ESP_LOGE(TAG, "unexpected first frame: %ux%u, %u bytes (want %ux%u, %u bytes)",
                 fb != NULL ? (unsigned)fb->width : 0U, fb != NULL ? (unsigned)fb->height : 0U,
                 fb != NULL ? (unsigned)fb->len : 0U, (unsigned)APP_CAM_SRC_WIDTH,
                 (unsigned)APP_CAM_SRC_HEIGHT, (unsigned)SRC_FRAME_BYTES);
        if (fb != NULL) {
            esp_camera_fb_return(fb);
        }
        (void)esp_camera_deinit();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_camera_fb_return(fb);
    s_ready = true;
    ESP_LOGI(TAG, "camera ready: %ux%u gray, stored frame %ux%u (%u bytes)", (unsigned)APP_CAM_SRC_WIDTH,
             (unsigned)APP_CAM_SRC_HEIGHT, (unsigned)APP_FRAME_WIDTH, (unsigned)APP_FRAME_HEIGHT,
             (unsigned)APP_FRAME_BYTES);
    return ESP_OK;
}

camera_fb_t *camera_cap_acquire(uint32_t timeout_ms)
{
    if (!s_ready || xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return NULL;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!frame_is_valid(fb)) {
        ESP_LOGW(TAG, "bad or missing frame");
        if (fb != NULL) {
            esp_camera_fb_return(fb);
        }
        xSemaphoreGive(s_lock);
        return NULL;
    }
    return fb;
}

void camera_cap_release(camera_fb_t *fb)
{
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }
    xSemaphoreGive(s_lock);
}

void camera_cap_crop(const camera_fb_t *fb, camera_fb_t *out)
{
    *out = *fb;
    out->buf = fb->buf + APP_CROP_OFFSET_BYTES;
    out->len = APP_FRAME_BYTES;
    out->height = APP_FRAME_HEIGHT;
}

esp_err_t camera_cap_set_exposure_lock(bool lock)
{
    sensor_t *sensor = esp_camera_sensor_get();
    if (!s_ready || sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(EXPOSURE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    /* Switching auto exposure/gain off leaves the sensor at the values it had converged to */
    const int enable = lock ? 0 : 1;
    int rc = sensor->set_exposure_ctrl(sensor, enable);
    if (rc == 0) {
        rc = sensor->set_gain_ctrl(sensor, enable);
    }
    xSemaphoreGive(s_lock);
    if (rc != 0) {
        ESP_LOGE(TAG, "exposure %s failed", lock ? "lock" : "unlock");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "auto exposure/gain %s", lock ? "frozen" : "released");
    return ESP_OK;
}
