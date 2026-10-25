/**
 * @file node_store.h
 * @brief Persist the lighting config and energy statistics in NVS.
 *
 * NVS namespace "node": key "cfg" (sl_config_t), key "energy" (energy_stats_t),
 * each stored as a versioned blob. A blob with a different version or size is
 * ignored (defaults are used), so changing the structs never loads garbage.
 *
 * Flash wear: write the config only when it changes, energy every
 * ENERGY_SAVE_PERIOD (10 min) and on hour changes, never every cycle.
 */
#pragma once

#include "energy_stats.h"
#include "esp_err.h"
#include "sl_mqtt_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @return ESP_OK, or an error (e.g. ESP_ERR_NVS_NOT_FOUND on first boot) leaving *cfg untouched */
esp_err_t node_store_load_config(sl_config_t *cfg);
esp_err_t node_store_save_config(const sl_config_t *cfg);

/** @return ESP_OK, or an error leaving *stats untouched */
esp_err_t node_store_load_energy(energy_stats_t *stats);
esp_err_t node_store_save_energy(const energy_stats_t *stats);

#ifdef __cplusplus
}
#endif
