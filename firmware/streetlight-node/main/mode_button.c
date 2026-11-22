/**
 * @file mode_button.c
 * @brief GPIO0 polling task for the mode switch (see mode_button.h).
 */
#include "mode_button.h"

#include <stdint.h>

#include "app_config.h"
#include "boot_mode.h"
#include "button_click.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "mode_button"

#define TASK_STACK_BYTES 3072U
#define TASK_PRIORITY    2U /* low: a button needs no hard timing */

static void button_task(void *arg)
{
    (void)arg;
    button_click_t detector;
    button_click_init(&detector);
    for (;;) {
        const bool pressed = gpio_get_level((gpio_num_t)APP_MODE_BUTTON_GPIO) == 0; /* active low */
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (button_click_feed(&detector, pressed, now_ms) == BUTTON_EVT_DOUBLE_CLICK) {
            ESP_LOGI(TAG, "double click on GPIO%d", APP_MODE_BUTTON_GPIO);
            boot_mode_switch(); /* restarts, does not return */
        }
        vTaskDelay(pdMS_TO_TICKS(APP_MODE_BUTTON_POLL_MS));
    }
}

esp_err_t mode_button_start(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << APP_MODE_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(button_task, "mode_btn", TASK_STACK_BYTES, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
