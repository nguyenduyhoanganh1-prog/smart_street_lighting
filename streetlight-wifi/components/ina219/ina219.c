/**
 * @file ina219.c
 * @brief INA219 current/voltage monitor driver (see ina219.h).
 */
#include "ina219.h"

#include <stddef.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INA219_REG_CONFIG 0x00U
#define INA219_REG_SHUNT  0x01U /* signed, LSB = 10 uV */
#define INA219_REG_BUS    0x02U /* bits 15..3 = value (LSB 4 mV), bit 1 = CNVR, bit 0 = OVF */

#define INA219_CONFIG_RESET 0x8000U
/*
 * BRNG  [13]    = 0     -> 16 V bus range
 * PG    [12:11] = 00    -> +-40 mV shunt range
 * BADC  [10:7]  = 1100  -> 12-bit, 16 samples averaged (8.51 ms)
 * SADC  [6:3]   = 1111  -> 12-bit, 128 samples averaged (68.10 ms)
 * MODE  [2:0]   = 111   -> shunt and bus, continuous
 */
#define INA219_CONFIG_PROJECT 0x067FU

#define INA219_SHUNT_LSB_UV   10.0f
#define INA219_BUS_LSB_MV     4U
#define INA219_BUS_SHIFT      3U
#define INA219_BUS_OVF_MASK   0x0001U

#define INA219_RESET_DELAY_MS  10U
#define INA219_FIRST_CONV_MS   80U  /* 68.10 ms shunt x128 + 8.51 ms bus x16, rounded up */
#define INA219_XFER_TIMEOUT_MS 100

static esp_err_t write_reg(const ina219_t *ctx, uint8_t reg, uint16_t value)
{
    const uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFFU) };
    return i2c_master_transmit(ctx->dev, buf, sizeof(buf), INA219_XFER_TIMEOUT_MS);
}

/* Registers are 16-bit big-endian: write the register pointer, then read 2 bytes */
static esp_err_t read_reg(const ina219_t *ctx, uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    const esp_err_t err = i2c_master_transmit_receive(ctx->dev, &reg, 1, buf, sizeof(buf),
                                                      INA219_XFER_TIMEOUT_MS);
    if (err == ESP_OK) {
        *value = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    }
    return err;
}

esp_err_t ina219_init(ina219_t *ctx, i2c_master_bus_handle_t bus, uint8_t addr,
                      uint32_t scl_hz, uint16_t shunt_mohm)
{
    if (ctx == NULL || bus == NULL || shunt_mohm == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->dev == NULL) {
        const i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = scl_hz,
        };
        const esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &ctx->dev);
        if (err != ESP_OK) {
            return err;
        }
    }
    ctx->shunt_mohm = shunt_mohm;

    esp_err_t err = write_reg(ctx, INA219_REG_CONFIG, INA219_CONFIG_RESET);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(INA219_RESET_DELAY_MS) + 1);

    err = write_reg(ctx, INA219_REG_CONFIG, INA219_CONFIG_PROJECT);
    if (err != ESP_OK) {
        return err;
    }
    /* Read back: catches a wrong address answering or a flaky bus */
    uint16_t readback = 0;
    err = read_reg(ctx, INA219_REG_CONFIG, &readback);
    if (err != ESP_OK) {
        return err;
    }
    ctx->started_us = esp_timer_get_time();
    return (readback == INA219_CONFIG_PROJECT) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ina219_read(ina219_t *ctx, ina219_sample_t *out)
{
    if (ctx == NULL || ctx->dev == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Right after init the first averaged conversion is not finished yet */
    const int64_t elapsed_ms = (esp_timer_get_time() - ctx->started_us) / 1000;
    if (elapsed_ms >= 0 && elapsed_ms < (int64_t)INA219_FIRST_CONV_MS) {
        const uint32_t wait_ms = INA219_FIRST_CONV_MS - (uint32_t)elapsed_ms;
        vTaskDelay(pdMS_TO_TICKS(wait_ms) + 1);
    }

    uint16_t raw_shunt = 0;
    uint16_t raw_bus = 0;
    esp_err_t err = read_reg(ctx, INA219_REG_SHUNT, &raw_shunt);
    if (err == ESP_OK) {
        err = read_reg(ctx, INA219_REG_BUS, &raw_bus);
    }
    if (err != ESP_OK) {
        return err;
    }

    /* uV / milliohm = mA */
    const float shunt_uv = (float)(int16_t)raw_shunt * INA219_SHUNT_LSB_UV;
    out->current_ma = shunt_uv / (float)ctx->shunt_mohm;
    out->bus_v = (float)((uint32_t)(raw_bus >> INA219_BUS_SHIFT) * INA219_BUS_LSB_MV) / 1000.0f;
    out->power_mw = out->bus_v * out->current_ma;
    out->overflow = (raw_bus & INA219_BUS_OVF_MASK) != 0U;
    return ESP_OK;
}

esp_err_t ina219_deinit(ina219_t *ctx)
{
    if (ctx == NULL || ctx->dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = i2c_master_bus_rm_device(ctx->dev);
    ctx->dev = NULL;
    return err;
}
