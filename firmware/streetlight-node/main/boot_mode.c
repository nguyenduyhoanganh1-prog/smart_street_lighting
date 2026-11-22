/**
 * @file boot_mode.c
 * @brief Boot mode kept in RTC RAM across a software restart (see boot_mode.h).
 */
#include "boot_mode.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "boot_mode"

#define MODE_MAGIC_AI_STREAM 0xA15E57A1U /* any other value = normal mode */
#define LOG_FLUSH_MS         100U        /* let the log line leave the UART before the restart */

static RTC_NOINIT_ATTR uint32_t s_mode_word;
static app_mode_t s_current = APP_MODE_NORMAL;

app_mode_t boot_mode_get(void)
{
    /* Only a software restart (our own esp_restart) may carry the request over */
    const bool requested = esp_reset_reason() == ESP_RST_SW && s_mode_word == MODE_MAGIC_AI_STREAM;
    s_current = requested ? APP_MODE_AI_STREAM : APP_MODE_NORMAL;
    s_mode_word = requested ? MODE_MAGIC_AI_STREAM : 0U; /* RTC RAM is undefined after a power-on: clear it */
    return s_current;
}

void boot_mode_switch(void)
{
    const app_mode_t next = (s_current == APP_MODE_NORMAL) ? APP_MODE_AI_STREAM : APP_MODE_NORMAL;
    s_mode_word = (next == APP_MODE_AI_STREAM) ? MODE_MAGIC_AI_STREAM : 0U;
    ESP_LOGW(TAG, "switching to %s mode: restarting", next == APP_MODE_AI_STREAM ? "ai-stream" : "normal");
    vTaskDelay(pdMS_TO_TICKS(LOG_FLUSH_MS));
    esp_restart();
}
