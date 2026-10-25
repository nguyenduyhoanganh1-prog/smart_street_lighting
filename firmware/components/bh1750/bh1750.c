/**
 * @file bh1750.c
 * @brief BH1750 ambient light sensor driver (see bh1750.h).
 */
#include "bh1750.h"

#include <stddef.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BH1750_CMD_POWER_ON      0x01U
#define BH1750_CMD_RESET         0x07U /* clears the data register, only valid while powered on */
#define BH1750_CMD_CONT_H_RES    0x10U /* continuous, 1 lx resolution */

#define BH1750_FIRST_CONV_MS     180U  /* datasheet max conversion time (H-resolution mode) */
#define BH1750_COUNTS_PER_LUX    1.2f  /* datasheet: lux = raw / 1.2 */
#define BH1750_XFER_TIMEOUT_MS   100

static esp_err_t send_cmd(bh1750_t *ctx, uint8_t cmd)
{
    return i2c_master_transmit(ctx->dev, &cmd, 1, BH1750_XFER_TIMEOUT_MS);
}

esp_err_t bh1750_init(bh1750_t *ctx, i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz)
{
    if (ctx == NULL || bus == NULL) {
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

    esp_err_t err = send_cmd(ctx, BH1750_CMD_POWER_ON);
    if (err == ESP_OK) {
        err = send_cmd(ctx, BH1750_CMD_RESET);
    }
    if (err == ESP_OK) {
        err = send_cmd(ctx, BH1750_CMD_CONT_H_RES);
    }
    if (err == ESP_OK) {
        ctx->started_us = esp_timer_get_time();
    }
    return err;
}

esp_err_t bh1750_read_lux(bh1750_t *ctx, float *lux)
{
    if (ctx == NULL || ctx->dev == NULL || lux == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Right after init the first conversion is not finished yet */
    const int64_t elapsed_ms = (esp_timer_get_time() - ctx->started_us) / 1000;
    if (elapsed_ms >= 0 && elapsed_ms < (int64_t)BH1750_FIRST_CONV_MS) {
        const uint32_t wait_ms = BH1750_FIRST_CONV_MS - (uint32_t)elapsed_ms;
        vTaskDelay(pdMS_TO_TICKS(wait_ms) + 1);
    }

    uint8_t raw[2];
    const esp_err_t err = i2c_master_receive(ctx->dev, raw, sizeof(raw), BH1750_XFER_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t counts = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    *lux = (float)counts / BH1750_COUNTS_PER_LUX;
    return ESP_OK;
}

esp_err_t bh1750_deinit(bh1750_t *ctx)
{
    if (ctx == NULL || ctx->dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = i2c_master_bus_rm_device(ctx->dev);
    ctx->dev = NULL;
    return err;
}
