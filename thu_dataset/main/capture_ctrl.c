/**
 * @file capture_ctrl.c
 * @brief Capture logic of thu_dataset (see capture_ctrl.h).
 */
#include "capture_ctrl.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "camera_cap.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lamp_pwm.h"
#include "sd_store.h"

#define TAG "capture"

#define MIN_VALID_EPOCH_S  1700000000LL /* Nov 2023: anything older is a wrong browser clock */
#define MAX_CLOCK_JITTER_S 5            /* do not step the clock for smaller differences */
#define OP_LOCK_TIMEOUT_MS 5000U
#define GRAB_TIMEOUT_MS    2000U
#define AUTO_TASK_STACK    6144U
#define AUTO_TASK_PRIO     4U
#define AUTO_POLL_MS       100U         /* how often the auto task re-checks stop/period changes */

static const int LAMP_GPIOS[APP_LAMP_COUNT] = {
    APP_LAMP1_GPIO, APP_LAMP2_GPIO, APP_LAMP3_GPIO, APP_LAMP4_GPIO,
};

static StaticSemaphore_t s_state_lock_buf;
static StaticSemaphore_t s_op_lock_buf;
static SemaphoreHandle_t s_state_lock; /* short: guards s_status */
static SemaphoreHandle_t s_op_lock;    /* long: one grab + SD write or session change at a time */
static capture_status_t s_status;

/* ================================== Helpers ================================== */

static void state_lock(void)
{
    (void)xSemaphoreTake(s_state_lock, portMAX_DELAY);
}

static void state_unlock(void)
{
    (void)xSemaphoreGive(s_state_lock);
}

static int64_t now_epoch(void)
{
    const time_t now = time(NULL);
    return ((long long)now >= MIN_VALID_EPOCH_S) ? (int64_t)now : 0;
}

static void set_error(const char *message)
{
    state_lock();
    strlcpy(s_status.error, message, sizeof(s_status.error));
    state_unlock();
}

static void refresh_space(void)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    if (sd_store_space(&total, &free_bytes) == ESP_OK) {
        state_lock();
        s_status.sd_total_bytes = total;
        s_status.sd_free_bytes = free_bytes;
        state_unlock();
    }
}

static bool auto_is_on(uint32_t *period_s)
{
    state_lock();
    const bool on = s_status.auto_on;
    *period_s = s_status.period_s;
    state_unlock();
    return on;
}

/* ================================== Capture ================================== */

/* Caller holds s_op_lock */
static esp_err_t start_session_locked(void)
{
    state_lock();
    const bool locked = s_status.exposure_locked;
    state_unlock();

    uint32_t session_no = 0;
    const esp_err_t err = sd_store_begin_session(&session_no, now_epoch(), locked);
    if (err != ESP_OK) {
        set_error("Không tạo được thư mục phiên trên thẻ SD");
        return err;
    }
    state_lock();
    s_status.session_no = session_no;
    s_status.session_count = 0;
    state_unlock();
    return ESP_OK;
}

/* Caller holds s_op_lock */
static esp_err_t capture_locked(void)
{
    capture_status_t snap;
    capture_ctrl_get_status(&snap);
    if (!snap.cam_ok) {
        set_error("Camera chưa sẵn sàng");
        return ESP_ERR_INVALID_STATE;
    }
    if (!snap.sd_ok) {
        set_error("Chưa có thẻ SD (cắm thẻ rồi khởi động lại board)");
        return ESP_ERR_INVALID_STATE;
    }
    if (snap.session_no == 0U) {
        const esp_err_t err = start_session_locked();
        if (err != ESP_OK) {
            return err;
        }
        capture_ctrl_get_status(&snap);
    }

    camera_fb_t *fb = camera_cap_acquire(GRAB_TIMEOUT_MS);
    if (fb == NULL) {
        set_error("Không lấy được khung hình từ camera");
        return ESP_ERR_TIMEOUT;
    }
    camera_fb_t view;
    camera_cap_crop(fb, &view);

    const sd_frame_meta_t meta = {
        .lamp_pct = { snap.lamp_pct[0], snap.lamp_pct[1], snap.lamp_pct[2], snap.lamp_pct[3] },
        .exposure_locked = snap.exposure_locked,
        .epoch_s = now_epoch(),
    };
    char name[DATASET_FRAME_NAME_CAP];
    const esp_err_t err = sd_store_save_frame(snap.session_no, snap.label, snap.session_count + 1U,
                                              view.buf, view.len, &meta, name, sizeof(name));
    camera_cap_release(fb);
    if (err != ESP_OK) {
        set_error("Ghi thẻ SD lỗi (thẻ đầy hoặc bị rút?)");
        return err;
    }

    state_lock();
    s_status.session_count++;
    s_status.total_count++;
    strlcpy(s_status.last_file, name, sizeof(s_status.last_file));
    s_status.error[0] = '\0';
    state_unlock();
    refresh_space();
    ESP_LOGI(TAG, "saved %s (session %u)", name, (unsigned)snap.session_no);
    return ESP_OK;
}

static esp_err_t capture_one(void)
{
    if (xSemaphoreTake(s_op_lock, pdMS_TO_TICKS(OP_LOCK_TIMEOUT_MS)) != pdTRUE) {
        set_error("Đang bận chụp, thử lại sau");
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = capture_locked();
    (void)xSemaphoreGive(s_op_lock);
    return err;
}

/* Wait until `period_s` seconds have passed since `start`, or auto-capture is switched off */
static void wait_for_next_shot(TickType_t start)
{
    for (;;) {
        uint32_t period_s = 0;
        if (!auto_is_on(&period_s)) {
            return;
        }
        if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(period_s * 1000U)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(AUTO_POLL_MS));
    }
}

static void auto_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t period_s = 0;
        if (!auto_is_on(&period_s)) {
            vTaskDelay(pdMS_TO_TICKS(AUTO_POLL_MS));
            continue;
        }
        const TickType_t shot_start = xTaskGetTickCount();
        const esp_err_t err = capture_one();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "auto-capture stopped: %s", esp_err_to_name(err));
            state_lock();
            s_status.auto_on = false;
            state_unlock();
            continue;
        }
        wait_for_next_shot(shot_start);
    }
}

/* ================================= Public API ================================= */

esp_err_t capture_ctrl_init(bool camera_ok, bool sd_ok)
{
    s_state_lock = xSemaphoreCreateMutexStatic(&s_state_lock_buf);
    s_op_lock = xSemaphoreCreateMutexStatic(&s_op_lock_buf);

    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.label, DATASET_LABELS[0], sizeof(s_status.label));
    s_status.period_s = APP_DEFAULT_PERIOD_S;
    s_status.cam_ok = camera_ok;
    s_status.sd_ok = sd_ok;
    if (!camera_ok) {
        strlcpy(s_status.error, "Camera không khởi tạo được (xem log serial)", sizeof(s_status.error));
    } else if (!sd_ok) {
        strlcpy(s_status.error, "Không đọc được thẻ SD (xem log serial)", sizeof(s_status.error));
    }

    esp_err_t err = lamp_pwm_init(LAMP_GPIOS, APP_LAMP_COUNT, APP_PWM_FREQ_HZ, APP_LAMP_ACTIVE_LOW != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lamp PWM init failed: %s (check APP_LAMPx_GPIO)", esp_err_to_name(err));
    }
    if (sd_ok) {
        refresh_space();
    }
    if (xTaskCreate(auto_task, "auto_cap", AUTO_TASK_STACK, NULL, AUTO_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create the auto-capture task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void capture_ctrl_get_status(capture_status_t *out)
{
    state_lock();
    *out = s_status;
    state_unlock();
    out->time_valid = (now_epoch() != 0);
}

esp_err_t capture_ctrl_capture_now(void)
{
    return capture_one();
}

esp_err_t capture_ctrl_set_auto(bool on, uint32_t period_s)
{
    if (!on) {
        state_lock();
        s_status.auto_on = false;
        state_unlock();
        return ESP_OK;
    }
    if (!dataset_period_valid(period_s)) {
        return ESP_ERR_INVALID_ARG;
    }
    state_lock();
    const bool usable = s_status.cam_ok && s_status.sd_ok;
    if (usable) {
        s_status.period_s = period_s;
        s_status.auto_on = true;
        s_status.error[0] = '\0';
    }
    state_unlock();
    if (!usable) {
        set_error("Camera hoặc thẻ SD chưa sẵn sàng, không bật được chế độ tự động");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t capture_ctrl_set_label(const char *label)
{
    if (!dataset_label_valid(label)) {
        return ESP_ERR_INVALID_ARG;
    }
    state_lock();
    strlcpy(s_status.label, label, sizeof(s_status.label));
    state_unlock();
    return ESP_OK;
}

esp_err_t capture_ctrl_new_session(void)
{
    if (xSemaphoreTake(s_op_lock, pdMS_TO_TICKS(OP_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    state_lock();
    const bool sd_ok = s_status.sd_ok;
    state_unlock();
    if (sd_ok) {
        err = start_session_locked();
    } else {
        set_error("Chưa có thẻ SD (cắm thẻ rồi khởi động lại board)");
    }
    (void)xSemaphoreGive(s_op_lock);
    return err;
}

esp_err_t capture_ctrl_set_lamp(unsigned lamp, uint8_t pct)
{
    if (lamp > APP_LAMP_COUNT || pct > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    const unsigned first = (lamp == 0U) ? 0U : lamp - 1U;
    const unsigned last = (lamp == 0U) ? APP_LAMP_COUNT - 1U : lamp - 1U;
    esp_err_t result = ESP_OK;
    for (unsigned k = first; k <= last; k++) {
        const esp_err_t err = lamp_pwm_set(k, pct, 0U, 0U); /* instant: the next frame must match the level */
        if (err != ESP_OK) {
            result = err;
            continue;
        }
        state_lock();
        s_status.lamp_pct[k] = pct;
        state_unlock();
    }
    return result;
}

esp_err_t capture_ctrl_set_exposure_lock(bool lock)
{
    const esp_err_t err = camera_cap_set_exposure_lock(lock);
    if (err == ESP_OK) {
        state_lock();
        s_status.exposure_locked = lock;
        state_unlock();
    }
    return err;
}

esp_err_t capture_ctrl_set_time(int64_t epoch_s)
{
    if (epoch_s < MIN_VALID_EPOCH_S) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t current = now_epoch();
    if (current != 0) {
        const int64_t diff = current - epoch_s;
        if (diff >= -MAX_CLOCK_JITTER_S && diff <= MAX_CLOCK_JITTER_S) {
            return ESP_OK;
        }
    }
    const struct timeval tv = { .tv_sec = (time_t)epoch_s, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "clock set from browser: %lld", (long long)epoch_s);
    return ESP_OK;
}
