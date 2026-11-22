/**
 * @file ai_pipeline.c
 * @brief Capture -> JPEG -> detection -> lamp rule task (see ai_pipeline.h).
 */
#include "ai_pipeline.h"

#include <string.h>

#include "app_config.h"
#include "camera_cap.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "lamp_ctrl.h"
#include "road_zones.h"

#define TAG "ai_pipeline"

#define TASK_STACK_BYTES   16384U /* EI inference + the JPEG encoder run on this stack */
#define TASK_PRIORITY      5      /* below the camera driver task, above idle */
#define CAMERA_WAIT_MS     500U
#define CAMERA_RETRY_MS    200U
#define PUBLISH_LOCK_MS    200U
#define LOG_EVERY_CYCLES   20U    /* serial timing line every N published frames */

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} jpeg_sink_t;

static uint8_t *s_frame;        /* copy of the camera frame, PSRAM, allocated once at start */
static uint8_t *s_jpeg_work;    /* JPEG being encoded */
static uint8_t *s_jpeg_shared;  /* latest published JPEG */
static size_t s_jpeg_shared_len;
static ai_snapshot_t s_meta;
static bool s_exposure_locked;
static uint32_t s_cycle_count;
static road_zones_t s_road;           /* start-up calibration result, for the web page */
static ai_road_state_t s_road_state;   /* AI_ROAD_PENDING until calibrate_road() ends */
static volatile bool s_recalibrate;    /* set by the web server task, cleared by the pipeline task */
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;

/* frame2jpg_cb output sink: copy into the work buffer; returning less than len aborts the encoder */
static size_t jpeg_sink_cb(void *arg, size_t index, const void *data, size_t len)
{
    (void)index;
    jpeg_sink_t *sink = (jpeg_sink_t *)arg;
    if (len == 0U) {
        return 0U; /* end-of-image marker */
    }
    if (sink->len + len > sink->cap) {
        return 0U; /* does not fit: the encoder reports failure */
    }
    memcpy(sink->buf + sink->len, data, len);
    sink->len += len;
    return len;
}

static size_t encode_jpeg(void)
{
    camera_fb_t view = {
        .buf = s_frame,
        .len = APP_FRAME_BYTES,
        .width = APP_FRAME_WIDTH,
        .height = APP_FRAME_HEIGHT,
        .format = PIXFORMAT_GRAYSCALE,
    };
    jpeg_sink_t sink = { .buf = s_jpeg_work, .cap = APP_STREAM_JPEG_BYTES, .len = 0U };
    if (!frame2jpg_cb(&view, APP_STREAM_JPEG_QUALITY, jpeg_sink_cb, &sink)) {
        return 0U;
    }
    return sink.len;
}

/** Copy the camera frame into s_frame and give the camera back. @return false if no frame */
static bool grab_frame(void)
{
    camera_fb_t *fb = camera_cap_acquire(CAMERA_WAIT_MS);
    if (fb == NULL) {
        return false;
    }
    camera_fb_t view;
    camera_cap_crop(fb, &view);
    memcpy(s_frame, view.buf, APP_FRAME_BYTES);
    camera_cap_release(fb);
    return true;
}

static void publish(const detection_result_t *det, size_t jpeg_len, uint32_t loop_ms)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(PUBLISH_LOCK_MS)) != pdTRUE) {
        return; /* a slow reader: drop this frame, the next one follows */
    }
    memcpy(s_jpeg_shared, s_jpeg_work, jpeg_len);
    s_jpeg_shared_len = jpeg_len;
    s_meta.seq++;
    s_meta.det = *det;
    s_meta.jpeg_bytes = (uint32_t)jpeg_len;
    s_meta.loop_ms = loop_ms;
    s_meta.exposure_locked = s_exposure_locked;
    xSemaphoreGive(s_lock);
}

static void set_road_result(ai_road_state_t state, const road_zones_t *road)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (road != NULL) {
        s_road = *road;
    }
    s_road_state = state;
    xSemaphoreGive(s_lock);
}

static void log_road(const road_zones_t *road, uint32_t took_ms)
{
    ESP_LOGI(TAG, "road found in %u ms: x %d..%d, top y=%.3fx%+.1f, bottom y=%.3fx%+.1f", (unsigned)took_ms,
             road->x_min, road->x_max, (double)road->top_a, (double)road->top_b, (double)road->bottom_a,
             (double)road->bottom_b);
    if (road->has_left) {
        ESP_LOGI(TAG, "left end x=%.3fy%+.1f", (double)road->left_a, (double)road->left_b);
    }
    ESP_LOGI(TAG, "zone boundaries x = %.0f, %.0f, %.0f (zone k -> lamp k, lamp 1 on the left)",
             (double)road->boundaries_x[0], (double)road->boundaries_x[1], (double)road->boundaries_x[2]);
}

/** Put the lamps back to the levels and automatic mode saved before the calibration. */
static void restore_lamps(const uint8_t pct[APP_LAMP_COUNT], bool auto_on)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        (void)lamp_ctrl_set_manual(k + 1U, pct[k]);
    }
    if (auto_on) {
        lamp_ctrl_set_auto(true);
    }
}

/**
 * Road calibration (method D): every lamp at full level, wait for the exposure, take one frame, find
 * the road, its kerbs and its zones, then lamps back as they were. Runs before the first detection
 * and again on each request from the page. A failure falls back to the 4 quarters of the frame width:
 * the stream still works.
 */
static void calibrate_road(void)
{
    road_zones_work_t *work = heap_caps_malloc(sizeof(*work), MALLOC_CAP_SPIRAM); /* freed below */
    if (work == NULL) {
        ESP_LOGE(TAG, "no PSRAM for the road calibration: zones stay the 4 quarters of the frame");
        set_road_result(AI_ROAD_NOT_FOUND, NULL);
        return;
    }
    uint8_t lamps_before[APP_LAMP_COUNT];
    bool auto_before = false;
    lamp_ctrl_get(lamps_before, &auto_before); /* all off, auto off at start-up */
    (void)lamp_ctrl_set_manual(0U, APP_CALIB_LAMP_PCT);
    vTaskDelay(pdMS_TO_TICKS(APP_CALIB_SETTLE_MS));
    bool grabbed = false;
    for (unsigned i = 0; i <= APP_CALIB_SKIP_FRAMES; i++) {
        grabbed = grab_frame(); /* the last one is exposed with every lamp on */
    }
    road_zones_t road;
    const int64_t t0 = esp_timer_get_time();
    const bool found = grabbed && road_zones_calibrate(s_frame, APP_FRAME_WIDTH, APP_FRAME_HEIGHT, work, &road);
    const uint32_t took_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    heap_caps_free(work);
    restore_lamps(lamps_before, auto_before);
    if (!found) {
        ESP_LOGW(TAG, "road not found (%s): zones are the 4 quarters of the frame",
                 grabbed ? "no road edges in the frame" : "no camera frame");
        const road_zones_t no_road = { .valid = false };
        lamp_ctrl_set_road(&no_road); /* forget an older road: lamps and page both use the quarters */
        set_road_result(AI_ROAD_NOT_FOUND, NULL);
        return;
    }
    lamp_ctrl_set_road(&road);
    set_road_result(AI_ROAD_FOUND, &road);
    log_road(&road, took_ms);
}

static void pipeline_task(void *arg)
{
    (void)arg;
    detection_result_t det;
    calibrate_road();
    for (;;) {
        if (s_recalibrate) {
            s_recalibrate = false; /* cleared first: a request arriving during the run starts another one */
            ESP_LOGI(TAG, "road calibration requested from the page");
            calibrate_road();
        }
        const int64_t t0 = esp_timer_get_time();
        if (!grab_frame()) {
            vTaskDelay(pdMS_TO_TICKS(CAMERA_RETRY_MS));
            continue;
        }
        const size_t jpeg_len = encode_jpeg();
        if (jpeg_len == 0U) {
            ESP_LOGW(TAG, "JPEG encode failed or larger than %u bytes", (unsigned)APP_STREAM_JPEG_BYTES);
            vTaskDelay(pdMS_TO_TICKS(CAMERA_RETRY_MS));
            continue;
        }
        if (detector_run(s_frame, APP_FRAME_WIDTH, APP_FRAME_HEIGHT, &det) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(CAMERA_RETRY_MS));
            continue;
        }
        lamp_ctrl_update(&det, esp_timer_get_time() / 1000);
        const uint32_t loop_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        publish(&det, jpeg_len, loop_ms);
        if (++s_cycle_count % LOG_EVERY_CYCLES == 0U) {
            ESP_LOGI(TAG, "cycle %u ms (resize %u us, dsp %u us, inference %u us), %u object(s)",
                     (unsigned)loop_ms, (unsigned)det.resize_us, (unsigned)det.dsp_us, (unsigned)det.infer_us,
                     (unsigned)det.count);
        }
        vTaskDelay(1); /* let the idle task and WiFi run even when a cycle is fast */
    }
}

esp_err_t ai_pipeline_start(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    /* Allocated once at start and never freed (no heap churn while running) */
    s_frame = heap_caps_malloc(APP_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    s_jpeg_work = heap_caps_malloc(APP_STREAM_JPEG_BYTES, MALLOC_CAP_SPIRAM);
    s_jpeg_shared = heap_caps_malloc(APP_STREAM_JPEG_BYTES, MALLOC_CAP_SPIRAM);
    if (s_frame == NULL || s_jpeg_work == NULL || s_jpeg_shared == NULL) {
        ESP_LOGE(TAG, "out of PSRAM for the frame buffers");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(pipeline_task, "ai_pipeline", TASK_STACK_BYTES, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

size_t ai_pipeline_get_jpeg(uint8_t *dst, size_t cap, ai_snapshot_t *meta)
{
    size_t len = 0U;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(PUBLISH_LOCK_MS)) != pdTRUE) {
        return 0U;
    }
    if (s_jpeg_shared_len > 0U && s_jpeg_shared_len <= cap) {
        memcpy(dst, s_jpeg_shared, s_jpeg_shared_len);
        len = s_jpeg_shared_len;
        if (meta != NULL) {
            *meta = s_meta;
        }
    }
    xSemaphoreGive(s_lock);
    return len;
}

void ai_pipeline_get_snapshot(ai_snapshot_t *meta)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(PUBLISH_LOCK_MS)) != pdTRUE) {
        memset(meta, 0, sizeof(*meta));
        return;
    }
    *meta = s_meta;
    xSemaphoreGive(s_lock);
}

ai_road_state_t ai_pipeline_get_road(road_zones_t *road)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(PUBLISH_LOCK_MS)) != pdTRUE) {
        return AI_ROAD_PENDING; /* busy: the page asks again a second later */
    }
    const ai_road_state_t state = s_road_state;
    *road = s_road;
    xSemaphoreGive(s_lock);
    return state;
}

void ai_pipeline_request_recalibration(void)
{
    set_road_result(AI_ROAD_PENDING, NULL); /* the page polls /api/road until this changes */
    s_recalibrate = true;
}

esp_err_t ai_pipeline_set_exposure_lock(bool lock)
{
    const esp_err_t err = camera_cap_set_exposure_lock(lock);
    if (err == ESP_OK) {
        s_exposure_locked = lock;
    }
    return err;
}
