/**
 * @file lamp_pwm.c
 * @brief Lamp dimming with LEDC hardware fades (see lamp_pwm.h).
 */
#include "lamp_pwm.h"

#include <stddef.h>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "lamp_pwm"

#define LAMP_LEDC_MODE      LEDC_LOW_SPEED_MODE /* the only mode on ESP32-S3 */
#define LAMP_LEDC_TIMER     LEDC_TIMER_1
#define LAMP_LEDC_CH_BASE   2U                  /* channels 2..5 */
#define LAMP_LEDC_RES       LEDC_TIMER_13_BIT   /* 80 MHz / 5 kHz allows 13 bits */
#define LAMP_DUTY_MAX       ((1U << 13) - 1U)
#define LAMP_PERCENT_MAX    100U

typedef struct {
    uint8_t target;          /* last applied target, % */
    uint8_t pending;         /* fade-down waiting for a fade-up to finish */
    bool has_pending;
    bool fading_up;
    uint32_t pending_fade_ms;
    int64_t fade_end_us;     /* esp_timer time when the running fade ends */
} lamp_channel_t;

static lamp_channel_t s_lamps[LAMP_PWM_MAX_CHANNELS];
static unsigned s_count;
static bool s_ready;

static ledc_channel_t ledc_channel(unsigned lamp)
{
    return (ledc_channel_t)(LAMP_LEDC_CH_BASE + lamp);
}

static uint32_t percent_to_duty(uint8_t percent)
{
    return ((uint32_t)percent * LAMP_DUTY_MAX + LAMP_PERCENT_MAX / 2U) / LAMP_PERCENT_MAX;
}

static uint8_t duty_to_percent(uint32_t duty)
{
    if (duty >= LAMP_DUTY_MAX) {
        return (uint8_t)LAMP_PERCENT_MAX;
    }
    return (uint8_t)((duty * LAMP_PERCENT_MAX + LAMP_DUTY_MAX / 2U) / LAMP_DUTY_MAX);
}

static bool fade_running(const lamp_channel_t *c, int64_t now_us)
{
    return now_us < c->fade_end_us;
}

static esp_err_t start_fade(unsigned lamp, uint8_t percent, uint32_t fade_ms, bool up)
{
    lamp_channel_t *c = &s_lamps[lamp];
    const ledc_channel_t ch = ledc_channel(lamp);
    const uint32_t duty = percent_to_duty(percent);
    const int64_t now_us = esp_timer_get_time();
    esp_err_t err;

    /* Always stop first (no-op when idle): the hardware fade may outlast our
     * time estimate, and a new fade cannot start while one is running.
     * The duty stays at the level reached, so the next fade starts from there. */
    (void)ledc_fade_stop(LAMP_LEDC_MODE, ch);

    if (fade_ms == 0U) {
        err = ledc_set_duty(LAMP_LEDC_MODE, ch, duty);
        if (err == ESP_OK) {
            err = ledc_update_duty(LAMP_LEDC_MODE, ch);
        }
        c->fade_end_us = now_us;
    } else {
        err = ledc_set_fade_with_time(LAMP_LEDC_MODE, ch, duty, (int)fade_ms);
        if (err == ESP_OK) {
            err = ledc_fade_start(LAMP_LEDC_MODE, ch, LEDC_FADE_NO_WAIT);
        }
        c->fade_end_us = now_us + (int64_t)fade_ms * 1000;
    }
    c->target = percent;
    c->fading_up = up && fade_ms > 0U;
    if (err != ESP_OK) {
        c->fade_end_us = now_us; /* nothing is running, do not report "fading" */
        c->fading_up = false;
        ESP_LOGW(TAG, "lamp %u fade to %u%% failed: %s", lamp + 1U, (unsigned)percent,
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t lamp_pwm_init(const int gpios[], unsigned count, uint32_t freq_hz, bool active_low)
{
    if (gpios == NULL || count == 0U || count > LAMP_PWM_MAX_CHANNELS || freq_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LAMP_LEDC_MODE,
        .duty_resolution = LAMP_LEDC_RES,
        .timer_num = LAMP_LEDC_TIMER,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "timer config");

    for (unsigned k = 0; k < count; k++) {
        const ledc_channel_config_t ch_cfg = {
            .gpio_num = gpios[k],
            .speed_mode = LAMP_LEDC_MODE,
            .channel = ledc_channel(k),
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LAMP_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
            .flags.output_invert = active_low ? 1U : 0U,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "channel %u config", k);
        s_lamps[k] = (lamp_channel_t){ 0 };
    }
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "fade install");

    s_count = count;
    s_ready = true;
    ESP_LOGI(TAG, "%u lamps ready, %u Hz, 13-bit, %s", count, (unsigned)freq_hz,
             active_low ? "active-low (inverted)" : "active-high");
    return ESP_OK;
}

esp_err_t lamp_pwm_set(unsigned lamp, uint8_t percent, uint32_t fade_up_ms, uint32_t fade_down_ms)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (lamp >= s_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > LAMP_PERCENT_MAX) {
        percent = (uint8_t)LAMP_PERCENT_MAX;
    }
    lamp_channel_t *c = &s_lamps[lamp];
    const uint8_t wanted_now = c->has_pending ? c->pending : c->target;
    if (percent == wanted_now) {
        return ESP_OK;
    }

    const int64_t now_us = esp_timer_get_time();
    const bool running = fade_running(c, now_us);
    const uint8_t current = lamp_pwm_get(lamp);

    if (percent >= current) {
        /* Brighter: react immediately from the current level */
        c->has_pending = false;
        return start_fade(lamp, percent, fade_up_ms, true);
    }
    if (running && c->fading_up) {
        /* Darker while a fade-up is running: let the fade-up finish first */
        c->pending = percent;
        c->pending_fade_ms = fade_down_ms;
        c->has_pending = true;
        return ESP_OK;
    }
    c->has_pending = false;
    return start_fade(lamp, percent, fade_down_ms, false);
}

void lamp_pwm_service(void)
{
    if (!s_ready) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    for (unsigned k = 0; k < s_count; k++) {
        lamp_channel_t *c = &s_lamps[k];
        if (c->has_pending && !fade_running(c, now_us)) {
            c->has_pending = false;
            (void)start_fade(k, c->pending, c->pending_fade_ms, false);
        }
    }
}

uint8_t lamp_pwm_get(unsigned lamp)
{
    if (!s_ready || lamp >= s_count) {
        return 0;
    }
    const uint32_t duty = ledc_get_duty(LAMP_LEDC_MODE, ledc_channel(lamp));
    return (duty == LEDC_ERR_DUTY) ? 0U : duty_to_percent(duty);
}

uint8_t lamp_pwm_target(unsigned lamp)
{
    if (!s_ready || lamp >= s_count) {
        return 0;
    }
    const lamp_channel_t *c = &s_lamps[lamp];
    return c->has_pending ? c->pending : c->target;
}

bool lamp_pwm_is_steady(unsigned lamp)
{
    if (!s_ready || lamp >= s_count) {
        return false;
    }
    const lamp_channel_t *c = &s_lamps[lamp];
    return !c->has_pending && !fade_running(c, esp_timer_get_time());
}
