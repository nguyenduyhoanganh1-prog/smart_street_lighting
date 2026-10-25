/**
 * @file bh1750.h
 * @brief BH1750 ambient light sensor driver (I2C, ESP-IDF v5.x i2c_master API).
 *
 * Uses continuous high-resolution mode (1 lx, up to 120 ms per conversion).
 * The bus is owned by the caller; this driver only adds one device to it.
 */
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BH1750_ADDR_LOW  0x23 /* ADDR pin to GND (this project) */
#define BH1750_ADDR_HIGH 0x5C /* ADDR pin to VCC */

/** Device context. Declare it static (zero-initialised); no heap use by the caller. */
typedef struct {
    i2c_master_dev_handle_t dev; /* NULL until the first bh1750_init() */
    int64_t started_us;          /* esp_timer time when continuous mode was started */
} bh1750_t;

/**
 * Add the device to the bus, power it on and start continuous measurement.
 * Safe to call again after an error (re-configures the same device).
 */
esp_err_t bh1750_init(bh1750_t *ctx, i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz);

/**
 * Read the latest illuminance.
 * Waits (once, right after init) until the first conversion is done.
 * @param[out] lux  illuminance in lux (0 .. ~54612)
 */
esp_err_t bh1750_read_lux(bh1750_t *ctx, float *lux);

/** Remove the device from the bus. */
esp_err_t bh1750_deinit(bh1750_t *ctx);

#ifdef __cplusplus
}
#endif
