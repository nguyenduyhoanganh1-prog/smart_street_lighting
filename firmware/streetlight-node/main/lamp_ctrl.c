/**
 * @file lamp_ctrl.c
 * @brief Manual / automatic lamp levels (see lamp_ctrl.h).
 */
#include "lamp_ctrl.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lamp_pwm.h"

#define TAG "lamp_ctrl"

#define LOCK_TIMEOUT_MS 200U
#define NEVER_SEEN_MS   INT64_MIN

static const int LAMP_GPIOS[APP_LAMP_COUNT] = {
    APP_LAMP1_GPIO, APP_LAMP2_GPIO, APP_LAMP3_GPIO, APP_LAMP4_GPIO,
};

_Static_assert(ROAD_ZONES_COUNT == APP_ZONE_COUNT, "one road zone per lamp");

static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static bool s_auto;
static road_zones_t s_road; /* valid == false until the start-up calibration found the road */
/* When an object was last seen in or next to each lamp's zone (monotonic ms) */
static int64_t s_last_seen_ms[APP_LAMP_COUNT];

static bool take(void)
{
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

/** Quarter of the frame width an x position (0..1) falls in, 0..APP_ZONE_COUNT-1. */
static unsigned zone_of(float x)
{
    if (x <= 0.0f) {
        return 0U;
    }
    const unsigned zone = (unsigned)(x * (float)APP_ZONE_COUNT);
    return zone < APP_ZONE_COUNT ? zone : APP_ZONE_COUNT - 1U;
}

/**
 * Zone of a detection. With a calibrated road: false (ignore it) when its centre is off the road.
 * Without one: the quarter of the frame width, as before.
 */
static bool object_zone(const detection_t *det, unsigned *zone)
{
    if (!s_road.valid) {
        *zone = zone_of(det->cx);
        return true;
    }
    const float x = det->cx * (float)APP_FRAME_WIDTH; /* detections are in 0..1 of the frame */
    const float y = det->cy * (float)APP_FRAME_HEIGHT;
    if (!road_zones_contains(&s_road, x, y)) {
        return false;
    }
    *zone = road_zones_zone_of(&s_road, x);
    return true;
}

/** Remember that an object is in `zone`: lamps zone-1 .. zone+1 must be lit. */
static void mark_zone_seen(unsigned zone, int64_t now_ms)
{
    const unsigned first = zone > 0U ? zone - 1U : 0U;
    const unsigned last = (zone + 1U < APP_LAMP_COUNT) ? zone + 1U : APP_LAMP_COUNT - 1U;
    for (unsigned k = first; k <= last; k++) {
        s_last_seen_ms[k] = now_ms;
    }
}

static void reset_seen(void)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        s_last_seen_ms[k] = NEVER_SEEN_MS;
    }
}

esp_err_t lamp_ctrl_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    reset_seen();
    s_auto = false;
    const esp_err_t err = lamp_pwm_init(LAMP_GPIOS, APP_LAMP_COUNT, APP_PWM_FREQ_HZ, APP_LAMP_ACTIVE_LOW != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lamp PWM init failed: %s (check APP_LAMPx_GPIO)", esp_err_to_name(err));
    }
    return err;
}

esp_err_t lamp_ctrl_set_manual(unsigned lamp, uint8_t pct)
{
    if (lamp > APP_LAMP_COUNT || pct > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!take()) {
        return ESP_ERR_TIMEOUT;
    }
    s_auto = false;
    const unsigned first = (lamp == 0U) ? 0U : lamp - 1U;
    const unsigned last = (lamp == 0U) ? APP_LAMP_COUNT - 1U : lamp - 1U;
    esp_err_t result = ESP_OK;
    for (unsigned k = first; k <= last; k++) {
        const esp_err_t err = lamp_pwm_set(k, pct, 0U, 0U);
        if (err != ESP_OK) {
            result = err;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

void lamp_ctrl_set_auto(bool enable)
{
    if (!take()) {
        return;
    }
    if (enable && !s_auto) {
        reset_seen();
    }
    s_auto = enable;
    xSemaphoreGive(s_lock);
}

void lamp_ctrl_update(const detection_result_t *det, int64_t now_ms)
{
    if (!take()) {
        return;
    }
    if (s_auto) {
        if (det != NULL) {
            for (uint32_t i = 0; i < det->count; i++) {
                unsigned zone = 0U;
                if (object_zone(&det->items[i], &zone)) {
                    mark_zone_seen(zone, now_ms);
                }
            }
        }
        for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
            const bool lit = s_last_seen_ms[k] != NEVER_SEEN_MS && (now_ms - s_last_seen_ms[k]) < (int64_t)APP_AI_HOLD_MS;
            (void)lamp_pwm_set(k, lit ? APP_AI_ACTIVE_PCT : APP_AI_BASE_DIM_PCT, APP_AI_FADE_UP_MS,
                               APP_AI_FADE_DOWN_MS);
        }
    }
    lamp_pwm_service();
    xSemaphoreGive(s_lock);
}

void lamp_ctrl_set_road(const road_zones_t *road)
{
    if (road == NULL || !take()) {
        return;
    }
    s_road = *road;
    reset_seen(); /* zones changed: old "last seen" times belong to the old zones */
    xSemaphoreGive(s_lock);
}

void lamp_ctrl_get(uint8_t pct[APP_LAMP_COUNT], bool *auto_on)
{
    const bool locked = take();
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        pct[k] = lamp_pwm_get(k);
    }
    if (auto_on != NULL) {
        *auto_on = s_auto;
    }
    if (locked) {
        xSemaphoreGive(s_lock);
    }
}
