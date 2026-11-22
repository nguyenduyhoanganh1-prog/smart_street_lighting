/**
 * @file vision.c
 * @brief Camera + FOMO + road calibration task (see vision.h).
 */
#include "vision.h"

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
#include "road_zones.h"

#define TAG "vision"

#define TASK_STACK_BYTES   16384U /* FOMO inference + the SDK run on this stack */
#define TASK_PRIORITY      4U     /* below the control task, above the network task */
#define CAMERA_WAIT_MS     500U
#define DAY_POLL_MS        1000U
#define MAX_FAILED_FRAMES  5U     /* this many frames lost in a row -> camera_ok = false */
#define FPS_WINDOW_MS      2000
#define SNAPSHOT_LOCK_MS   100U

static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static vision_snapshot_t s_snap;
static uint8_t *s_frame; /* PSRAM, allocated once */
static volatile bool s_night;
static volatile bool s_calibrating;
static volatile bool s_recalibrate;
static volatile bool s_boot_calibration = true; /* every start-up finds the road again, day or night */
static volatile bool s_control_alive;           /* the control task runs: it can put every lamp at full level */
/* Last calibration attempt for the dashboard: own lock, so JPEG encoding never delays vision_get() */
static StaticSemaphore_t s_calib_lock_buf;
static SemaphoreHandle_t s_calib_lock;
static uint8_t *s_calib_jpeg; /* PSRAM, APP_CALIB_JPEG_BYTES, allocated once */
static vision_calib_info_t s_calib_info;
/* Stopped-object alert picture (also under s_calib_lock): PSRAM, APP_CALIB_JPEG_BYTES, allocated once */
static uint8_t *s_alert_jpeg;
static size_t s_alert_len;
static uint32_t s_alert_done_id;        /* id of the picture in s_alert_jpeg, 0 = none */
static volatile uint32_t s_alert_want_id;
static detection_result_t s_alert_det;  /* detections of the frame in s_alert_jpeg */
static void store_alert_image(const detection_result_t *det);
static unsigned s_failed_frames;
static unsigned s_window_frames;
static int64_t s_window_start_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int64_t s_grab_ms; /* when the frame now in s_frame was taken from the camera (latency log) */

/** Copy the camera frame into s_frame and give the camera back. */
static bool grab_frame(void)
{
    camera_fb_t *fb = camera_cap_acquire(CAMERA_WAIT_MS);
    if (fb == NULL) {
        return false;
    }
    s_grab_ms = now_ms();
    camera_fb_t view;
    camera_cap_crop(fb, &view);
    memcpy(s_frame, view.buf, APP_FRAME_BYTES);
    camera_cap_release(fb);
    return true;
}

static void update_snapshot(bool camera_ok, const detection_result_t *det)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) != pdTRUE) {
        return; /* a reader is slow: the next frame follows */
    }
    s_snap.camera_ok = camera_ok;
    if (det != NULL) {
        s_snap.det = *det;
        s_snap.t_ms = now_ms();
        s_snap.frame_ms = s_grab_ms;
    }
    xSemaphoreGive(s_lock);
}

static void update_fps(int64_t now)
{
    s_window_frames++;
    if (now - s_window_start_ms < FPS_WINDOW_MS) {
        return;
    }
    const float fps = (float)s_window_frames * 1000.0f / (float)(now - s_window_start_ms);
    s_window_frames = 0;
    s_window_start_ms = now;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) == pdTRUE) {
        s_snap.fps = fps;
        xSemaphoreGive(s_lock);
    }
}

/** One capture + inference; publishes the result. */
static void run_once(void)
{
    detection_result_t det;
    if (grab_frame() && detector_run(s_frame, APP_FRAME_WIDTH, APP_FRAME_HEIGHT, &det) == ESP_OK) {
        s_failed_frames = 0;
        update_snapshot(true, &det);
        update_fps(now_ms());
        store_alert_image(&det); /* no-op unless an alert asked for a picture */
        return;
    }
    s_failed_frames++;
    if (s_failed_frames == MAX_FAILED_FRAMES) {
        ESP_LOGE(TAG, "%u frames lost in a row: camera / model reported as faulty", MAX_FAILED_FRAMES);
        update_snapshot(false, NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(200U));
}

static void set_road(vision_road_state_t state, const road_zones_t *road)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_snap.road_state = state;
    if (road != NULL) {
        s_snap.road = *road;
    }
    xSemaphoreGive(s_lock);
}

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} jpeg_sink_t;

/* frame2jpg_cb output sink: returning less than len makes the encoder fail (JPEG too large) */
static size_t jpeg_sink_cb(void *arg, size_t index, const void *data, size_t len)
{
    (void)index;
    jpeg_sink_t *sink = (jpeg_sink_t *)arg;
    if (len == 0U || sink->len + len > sink->cap) {
        return 0U;
    }
    memcpy(sink->buf + sink->len, data, len);
    sink->len += len;
    return len;
}

/* s_frame -> JPEG into buf (cap bytes). @return JPEG length, 0 on failure (or larger than cap) */
static size_t encode_frame(uint8_t *buf, size_t cap)
{
    camera_fb_t view = {
        .buf = s_frame,
        .len = APP_FRAME_BYTES,
        .width = APP_FRAME_WIDTH,
        .height = APP_FRAME_HEIGHT,
        .format = PIXFORMAT_GRAYSCALE,
    };
    jpeg_sink_t sink = { .buf = buf, .cap = cap, .len = 0U };
    return frame2jpg_cb(&view, APP_CALIB_JPEG_QUALITY, jpeg_sink_cb, &sink) ? sink.len : 0U;
}

/* Alert picture wanted by the control task (vision_request_alert_image): the next analysed frame, kept
 * together with ITS detections so every dot the dashboard draws matches the picture */
static void store_alert_image(const detection_result_t *det)
{
    const uint32_t want = s_alert_want_id;
    if (want == s_alert_done_id || s_alert_jpeg == NULL || s_calib_lock == NULL
        || xSemaphoreTake(s_calib_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) != pdTRUE) {
        return;
    }
    s_alert_det = *det;
    s_alert_len = encode_frame(s_alert_jpeg, APP_CALIB_JPEG_BYTES);
    s_alert_done_id = want;
    xSemaphoreGive(s_calib_lock);
    if (s_alert_len == 0U) {
        ESP_LOGW(TAG, "alert JPEG failed or larger than %u B: alert without picture", (unsigned)APP_CALIB_JPEG_BYTES);
    }
}

/** Keep this attempt for the dashboard: the calibration frame as JPEG + the result. */
static void store_calib_attempt(bool grabbed, bool found, const road_zones_t *road)
{
    if (s_calib_lock == NULL || xSemaphoreTake(s_calib_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) != pdTRUE) {
        return;
    }
    size_t jpeg_len = 0U;
    if (grabbed && s_calib_jpeg != NULL) {
        jpeg_len = encode_frame(s_calib_jpeg, APP_CALIB_JPEG_BYTES);
        if (jpeg_len == 0U) {
            ESP_LOGW(TAG, "calibration JPEG failed or larger than %u B: numbers only", (unsigned)APP_CALIB_JPEG_BYTES);
        }
    }
    s_calib_info.seq++;
    s_calib_info.t_ms = now_ms();
    s_calib_info.found = found;
    if (found) {
        s_calib_info.road = *road;
    }
    s_calib_info.jpeg_len = jpeg_len;
    xSemaphoreGive(s_calib_lock);
}

/**
 * Road calibration (method D): every lamp at full level (the control loop does that while
 * s_calibrating is set), wait for the camera exposure, take one frame, find the road and its zones.
 * @return true when the road was found
 */
static bool calibrate_road(void)
{
    road_zones_work_t *work = heap_caps_malloc(sizeof(*work), MALLOC_CAP_SPIRAM); /* freed below */
    if (work == NULL) {
        ESP_LOGE(TAG, "no PSRAM for the road calibration");
        return false;
    }
    s_calibrating = true;
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
    s_calibrating = false;
    store_calib_attempt(grabbed, found, &road); /* s_frame still holds the calibration frame */
    if (found) {
        set_road(VISION_ROAD_FOUND, &road);
        ESP_LOGI(TAG, "road found in %u ms: x %d..%d, zone boundaries x = %.0f, %.0f, %.0f", (unsigned)took_ms,
                 road.x_min, road.x_max, (double)road.boundaries_x[0], (double)road.boundaries_x[1],
                 (double)road.boundaries_x[2]);
    } else {
        ESP_LOGW(TAG, "road not found (%s)", grabbed ? "no road edges in the frame" : "no camera frame");
    }
    return found;
}

static void vision_task(void *arg)
{
    (void)arg;
    bool was_night = false;
    unsigned attempts = 0;
    int64_t last_attempt_ms = 0;
    int64_t last_check_ms = now_ms();
    s_window_start_ms = last_check_ms;

    for (;;) {
        const int64_t now = now_ms();
        if (!s_control_alive) {
            vTaskDelay(pdMS_TO_TICKS(100U)); /* the lamps cannot go to full level before the control task runs */
            continue;
        }
        if (s_night && !was_night) { /* a new night: fresh set of calibration attempts */
            was_night = true;
            attempts = 0;
            last_attempt_ms = 0;
            s_window_start_ms = now;
            s_window_frames = 0;
        } else if (!s_night) {
            was_night = false;
        }
        if (s_recalibrate) {
            s_recalibrate = false;
            s_boot_calibration = true;
            attempts = 0;
            last_attempt_ms = 0;
            set_road(VISION_ROAD_PENDING, NULL);
        }
        vision_snapshot_t snap;
        vision_get(&snap);
        /* The road is looked for after every start (day or night), then at the start of each night until found */
        const bool wanted = (s_night || s_boot_calibration) && snap.road_state != VISION_ROAD_FOUND;
        const bool due = last_attempt_ms == 0 || now - last_attempt_ms >= (int64_t)APP_CALIB_RETRY_MS;
        if (wanted && attempts < APP_CALIB_MAX_ATTEMPTS && due) {
            attempts++;
            last_attempt_ms = now;
            const bool found = calibrate_road();
            if (found) {
                s_boot_calibration = false;
            } else if (attempts >= APP_CALIB_MAX_ATTEMPTS) {
                s_boot_calibration = false; /* the next night tries again */
                set_road(VISION_ROAD_NOT_FOUND, NULL);
                ESP_LOGW(TAG, "road not found after %u attempts: zones are the quarters of the frame until the next night or a new request",
                         attempts);
            }
            continue;
        }
        if (!s_night) {
            /* A failed check is confirmed right away (up to MAX_FAILED_FRAMES frames, a few seconds)
             * instead of one frame per period, else an unplugged camera took 5 periods to be reported. */
            const bool confirming = s_failed_frames > 0U && s_failed_frames < MAX_FAILED_FRAMES;
            if (confirming || now - last_check_ms >= (int64_t)APP_CAMERA_CHECK_PERIOD_MS) {
                last_check_ms = now;
                run_once(); /* a broken camera must be known before dark, not at dusk */
            }
            vTaskDelay(pdMS_TO_TICKS(DAY_POLL_MS));
            continue;
        }
        run_once();
        vTaskDelay(1); /* let the idle task and WiFi run even when a cycle is fast */
    }
}

esp_err_t vision_start(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    memset(&s_snap, 0, sizeof(s_snap));

    esp_err_t err = camera_cap_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed (%s): time-based lighting mode, check the camera wiring", esp_err_to_name(err));
        return err;
    }
    err = detector_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "model init failed (%s): time-based lighting mode", esp_err_to_name(err));
        return err;
    }
    s_frame = heap_caps_malloc(APP_FRAME_BYTES, MALLOC_CAP_SPIRAM); /* allocated once, never freed */
    if (s_frame == NULL) {
        ESP_LOGE(TAG, "out of PSRAM for the frame buffer");
        return ESP_ERR_NO_MEM;
    }
    s_calib_lock = xSemaphoreCreateMutexStatic(&s_calib_lock_buf);
    s_calib_jpeg = heap_caps_malloc(APP_CALIB_JPEG_BYTES, MALLOC_CAP_SPIRAM); /* once, never freed */
    if (s_calib_jpeg == NULL) {
        ESP_LOGW(TAG, "no PSRAM for the calibration JPEG: the dashboard gets the numbers only");
    }
    s_alert_jpeg = heap_caps_malloc(APP_CALIB_JPEG_BYTES, MALLOC_CAP_SPIRAM); /* once, never freed */
    if (s_alert_jpeg == NULL) {
        ESP_LOGW(TAG, "no PSRAM for the alert JPEG: alerts go without picture");
    }
    s_snap.camera_ok = true; /* until a frame fails: the first result needs about a second */
    if (xTaskCreate(vision_task, "vision", TASK_STACK_BYTES, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "vision task started");
    return ESP_OK;
}

void vision_set_night(bool night)
{
    s_night = night;
    s_control_alive = true; /* first call = the control task is running */
}

bool vision_calibrating(void)
{
    return s_calibrating;
}

void vision_request_recalibration(void)
{
    s_recalibrate = true;
}

void vision_get(vision_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) == pdTRUE) {
        *out = s_snap;
        xSemaphoreGive(s_lock);
        return;
    }
    memset(out, 0, sizeof(*out)); /* no snapshot (init failed or busy): camera_ok = false */
}

uint32_t vision_calib_seq(void)
{
    return s_calib_info.seq; /* one aligned 32-bit word: a torn read cannot happen */
}

bool vision_get_calib(uint8_t *jpeg, size_t cap, vision_calib_info_t *info)
{
    if (jpeg == NULL || info == NULL || s_calib_lock == NULL ||
        xSemaphoreTake(s_calib_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) != pdTRUE) {
        return false;
    }
    *info = s_calib_info;
    if (info->jpeg_len > cap) {
        info->jpeg_len = 0U; /* caller's buffer too small: numbers only */
    }
    if (info->jpeg_len > 0U) {
        memcpy(jpeg, s_calib_jpeg, info->jpeg_len);
    }
    xSemaphoreGive(s_calib_lock);
    return info->seq != 0U;
}

bool vision_get_alert_det(uint32_t id, detection_result_t *det)
{
    if (id == 0U || det == NULL || s_alert_done_id != id || s_calib_lock == NULL
        || xSemaphoreTake(s_calib_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) != pdTRUE) {
        return false;
    }
    const bool ok = s_alert_done_id == id;
    if (ok) {
        *det = s_alert_det;
    }
    xSemaphoreGive(s_calib_lock);
    return ok;
}

void vision_request_alert_image(uint32_t id)
{
    s_alert_want_id = id;
}

bool vision_get_alert_image(uint32_t id, uint8_t *jpeg, size_t cap, size_t *len, detection_result_t *det)
{
    if (id == 0U || jpeg == NULL || len == NULL || s_alert_done_id != id || s_calib_lock == NULL
        || xSemaphoreTake(s_calib_lock, pdMS_TO_TICKS(SNAPSHOT_LOCK_MS)) != pdTRUE) {
        return false;
    }
    const bool ok = s_alert_done_id == id && s_alert_len > 0U && s_alert_len <= cap;
    *len = ok ? s_alert_len : 0U;
    if (ok) {
        memcpy(jpeg, s_alert_jpeg, s_alert_len);
    }
    if (det != NULL) {
        *det = s_alert_det;
    }
    xSemaphoreGive(s_calib_lock);
    return s_alert_done_id == id; /* true even when the JPEG failed: nothing more will come for this id */
}
