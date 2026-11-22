/**
 * @file log_view.c
 * @brief Serial-monitor key -> log view (see log_view.h). TEMPORARY, for plans/eval.md.
 */
#include "log_view.h"

#include <stdatomic.h>
#include <stdint.h>

#include "app_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "log_view"

#define CONSOLE_UART     UART_NUM_0  /* CONFIG_ESP_CONSOLE_UART_NUM: the port idf.py monitor talks to */
#define UART_RX_BUF      256         /* driver minimum is 128 + a little */
#define KEY_TASK_STACK   2560U
#define KEY_TASK_PRIO    2U          /* below the control (6) and network tasks: keys are not urgent */

static atomic_int s_view = LOG_ALL;

static const char *const VIEW_TEXT[] = {
    "0 = ALL (normal log)",
    "1 = AI: fps, persons/vehicles",
    "2 = ZONES: road calibration, zone of each object",
    "3 = CONTROL: cycle time, detection->lamp latency, duty",
    "4 = ENERGY: V / I / P / mWh per lamp",
    "5 = ALERTS: stopped vehicles, lamp faults, sensor faults",
    "6 = SYSTEM: RAM, uptime, MQTT, web latencies",
};

static void set_view(int view)
{
    atomic_store(&s_view, view);
    /* In a section view only main.c ("node") keeps its info lines (filtered by log_view_show);
     * other components drop to warnings. "vision" logs the road calibration, wanted in views 1-2. */
    const bool all = view == LOG_ALL;
    esp_log_level_set("*", all ? ESP_LOG_INFO : ESP_LOG_WARN);
    esp_log_level_set("node", ESP_LOG_INFO);
    esp_log_level_set("vision", (all || view == LOG_AI || view == LOG_ZONE) ? ESP_LOG_INFO : ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "view %s  (keys: 0 all, 1 AI, 2 zones, 3 control, 4 energy, 5 alerts, 6 system)",
             VIEW_TEXT[view]);
}

static void key_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(CONSOLE_UART, &c, 1, portMAX_DELAY) == 1 && c >= '0' && c <= '6') {
            set_view(c - '0');
        }
    }
}

void log_view_start(void)
{
    if (!APP_EVAL_LOGS) {
        return;
    }
    /* RX only: the log output keeps going through the console as before */
    const esp_err_t err = uart_driver_install(CONSOLE_UART, UART_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot read the serial monitor (%s): log views off", esp_err_to_name(err));
        return;
    }
    if (xTaskCreate(key_task, "log_view", KEY_TASK_STACK, NULL, KEY_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no memory for the key task: log views off");
        return;
    }
    ESP_LOGI(TAG, "type 0-6 in the monitor to filter the log (0 all, 1 AI, 2 zones, 3 control, 4 energy, "
                  "5 alerts, 6 system)");
}

bool log_view_show(log_section_t section)
{
    const int view = atomic_load(&s_view);
    return view == LOG_ALL || view == (int)section;
}
