/**
 * @file sensor_hub.c
 * @brief I2C sensor bus: BH1750 + 4x INA219 (see sensor_hub.h).
 */
#include "sensor_hub.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "bh1750.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "sensor_hub"

#define I2C_GLITCH_FILTER   7
#define I2C_PROBE_TIMEOUT_MS 50
#define I2C_ADDR_FIRST      0x08U /* valid 7-bit range for a bus scan */
#define I2C_ADDR_LAST       0x77U

static const uint8_t INA_ADDR[APP_LAMP_COUNT] = {
    APP_INA219_ADDR_1, APP_INA219_ADDR_2, APP_INA219_ADDR_3, APP_INA219_ADDR_4,
};

static i2c_master_bus_handle_t s_bus;
static bh1750_t s_bh;
static ina219_t s_ina[APP_LAMP_COUNT];
static bool s_bh_online;
static bool s_ina_online[APP_LAMP_COUNT];
static int64_t s_last_retry_us;

static void scan_bus(void)
{
    ESP_LOGI(TAG, "I2C scan (port %d, SDA=GPIO%d, SCL=GPIO%d):",
             APP_I2C_PORT, APP_I2C_SDA_GPIO, APP_I2C_SCL_GPIO);
    unsigned found = 0;
    for (uint8_t addr = I2C_ADDR_FIRST; addr <= I2C_ADDR_LAST; addr++) {
        if (i2c_master_probe(s_bus, addr, I2C_PROBE_TIMEOUT_MS) == ESP_OK) {
            ESP_LOGI(TAG, "  found 0x%02X", addr);
            found++;
        }
    }
    if (found == 0U) {
        ESP_LOGW(TAG, "  nothing found: check 3.3 V, GND and SDA/SCL wiring");
    } else {
        ESP_LOGI(TAG, "  %u device(s); expected 0x23, 0x40, 0x41, 0x44, 0x45", found);
    }
}

/* verbose: warn on failure (boot); quiet retries only log at debug level */
static void log_init(const char *name, uint8_t addr, esp_err_t err, bool verbose)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s (0x%02X) ready", name, addr);
    } else if (verbose) {
        ESP_LOGW(TAG, "%s (0x%02X) not responding: %s", name, addr, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "%s (0x%02X) retry failed: %s", name, addr, esp_err_to_name(err));
    }
}

static void init_offline_devices(bool verbose)
{
    if (!s_bh_online) {
        const esp_err_t err = bh1750_init(&s_bh, s_bus, APP_BH1750_ADDR, APP_I2C_FREQ_HZ);
        s_bh_online = (err == ESP_OK);
        log_init("BH1750", APP_BH1750_ADDR, err, verbose);
    }
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        if (s_ina_online[k]) {
            continue;
        }
        const esp_err_t err = ina219_init(&s_ina[k], s_bus, INA_ADDR[k], APP_I2C_FREQ_HZ,
                                          APP_INA219_SHUNT_MOHM);
        s_ina_online[k] = (err == ESP_OK);
        char name[16];
        (void)snprintf(name, sizeof(name), "INA219 #%u", k + 1U);
        log_init(name, INA_ADDR[k], err, verbose);
    }
}

esp_err_t sensor_hub_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = APP_I2C_PORT,
        .sda_io_num = APP_I2C_SDA_GPIO,
        .scl_io_num = APP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_FILTER,
        .flags.enable_internal_pullup = true, /* modules also have 10k pull-ups */
    };
    const esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }
    scan_bus();
    init_offline_devices(true);
    s_last_retry_us = esp_timer_get_time();
    return ESP_OK;
}

void sensor_hub_sample(sensor_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (s_bus == NULL) {
        out->timestamp_us = esp_timer_get_time();
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_retry_us >= (int64_t)APP_DEVICE_RETRY_MS * 1000) {
        s_last_retry_us = now_us;
        init_offline_devices(false);
    }

    if (s_bh_online) {
        const esp_err_t err = bh1750_read_lux(&s_bh, &out->lux);
        out->lux_ok = (err == ESP_OK);
        if (!out->lux_ok) {
            ESP_LOGW(TAG, "BH1750 read failed (%s), marked offline", esp_err_to_name(err));
            s_bh_online = false;
        }
    }
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        if (!s_ina_online[k]) {
            continue;
        }
        const esp_err_t err = ina219_read(&s_ina[k], &out->ina[k]);
        out->ina_ok[k] = (err == ESP_OK);
        if (!out->ina_ok[k]) {
            ESP_LOGW(TAG, "INA219 #%u read failed (%s), marked offline", k + 1U, esp_err_to_name(err));
            s_ina_online[k] = false;
        }
    }
    out->timestamp_us = esp_timer_get_time();
}
