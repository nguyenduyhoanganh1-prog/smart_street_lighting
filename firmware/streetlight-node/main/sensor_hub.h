/**
 * @file sensor_hub.h
 * @brief Owns the I2C sensor bus and the BH1750 + 4x INA219 devices.
 *
 * A device that stops answering is marked offline and re-initialised every
 * APP_DEVICE_RETRY_MS, so unplugging/replugging a module recovers by itself.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"
#include "ina219.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t timestamp_us;                     /* esp_timer time after the reads */
    bool lux_ok;
    float lux;
    bool ina_ok[APP_LAMP_COUNT];
    ina219_sample_t ina[APP_LAMP_COUNT];      /* valid only where ina_ok[k] */
} sensor_snapshot_t;

/** Create the I2C bus, log a bus scan and initialise every sensor. */
esp_err_t sensor_hub_init(void);

/** Read every online sensor (blocking, a few ms per device). */
void sensor_hub_sample(sensor_snapshot_t *out);

#ifdef __cplusplus
}
#endif
