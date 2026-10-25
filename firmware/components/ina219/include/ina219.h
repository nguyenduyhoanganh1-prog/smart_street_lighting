/**
 * @file ina219.h
 * @brief INA219 current/voltage monitor driver (I2C, ESP-IDF v5.x i2c_master API).
 *
 * Configuration (reference/ina219-module.md section 5):
 *   bus range 16 V, PGA +-40 mV (max 400 mA with 0.1 ohm shunt),
 *   bus ADC 12-bit x16 averaging, shunt ADC 12-bit x128 averaging (~68 ms),
 *   continuous shunt + bus conversion.
 * Current is computed from the shunt voltage register (no calibration register),
 * so the resolution is 10 uV / R_shunt = 0.1 mA for a 0.1 ohm shunt.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INA219_ADDR_BASE 0x40 /* + A0 (1) + A1 (4): 0x40, 0x41, 0x44, 0x45 */

/** Device context. Declare it static (zero-initialised). */
typedef struct {
    i2c_master_dev_handle_t dev; /* NULL until the first ina219_init() */
    uint16_t shunt_mohm;         /* shunt resistance in milliohm (module "R100" = 100) */
    int64_t started_us;          /* esp_timer time when the configuration was written */
} ina219_t;

typedef struct {
    float bus_v;      /* voltage at Vin- (load side) vs GND, V */
    float current_ma; /* negative when Vin+ / Vin- are swapped */
    float power_mw;   /* bus_v * current_ma */
    bool overflow;    /* math overflow flag (OVF): current/power out of range */
} ina219_sample_t;

/**
 * Add the device to the bus, reset it, write the project configuration and
 * verify it by reading it back. Safe to call again after an error.
 */
esp_err_t ina219_init(ina219_t *ctx, i2c_master_bus_handle_t bus, uint8_t addr,
                      uint32_t scl_hz, uint16_t shunt_mohm);

/**
 * Read the latest averaged shunt and bus conversion.
 * Waits (once, right after init) until the first conversion (~77 ms) is done,
 * otherwise the registers still read 0.
 */
esp_err_t ina219_read(ina219_t *ctx, ina219_sample_t *out);

/** Remove the device from the bus. */
esp_err_t ina219_deinit(ina219_t *ctx);

#ifdef __cplusplus
}
#endif
