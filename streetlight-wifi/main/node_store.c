/**
 * @file node_store.c
 * @brief Persist config and energy statistics in NVS (see node_store.h).
 */
#include "node_store.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "nvs.h"

#define NODE_NAMESPACE   "node"
#define KEY_CONFIG       "cfg"
#define KEY_ENERGY       "energy"
#define CONFIG_VERSION   1U
#define ENERGY_VERSION   1U

typedef struct {
    uint32_t version;
    sl_config_t cfg;
} config_blob_t;

typedef struct {
    uint32_t version;
    energy_stats_t stats;
} energy_blob_t;

/* Static: ~950 B, too big for the caller's stack. Only used from the main task. */
static energy_blob_t s_energy_blob;

static esp_err_t load_blob(const char *key, void *buf, size_t size)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NODE_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = size;
    err = nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    if (err == ESP_OK && len != size) {
        err = ESP_ERR_INVALID_SIZE; /* struct layout changed since it was saved */
    }
    return err;
}

static esp_err_t save_blob(const char *key, const void *buf, size_t size)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NODE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, key, buf, size);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t node_store_load_config(sl_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    config_blob_t blob;
    esp_err_t err = load_blob(KEY_CONFIG, &blob, sizeof(blob));
    if (err != ESP_OK) {
        return err;
    }
    if (blob.version != CONFIG_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    /* Never trust stored strings to be terminated */
    blob.cfg.night_start[SL_HHMM_LEN - 1] = '\0';
    blob.cfg.night_end[SL_HHMM_LEN - 1] = '\0';
    *cfg = blob.cfg;
    return ESP_OK;
}

esp_err_t node_store_save_config(const sl_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const config_blob_t blob = { .version = CONFIG_VERSION, .cfg = *cfg };
    return save_blob(KEY_CONFIG, &blob, sizeof(blob));
}

esp_err_t node_store_load_energy(energy_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = load_blob(KEY_ENERGY, &s_energy_blob, sizeof(s_energy_blob));
    if (err != ESP_OK) {
        return err;
    }
    if (s_energy_blob.version != ENERGY_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    for (unsigned k = 0; k < ENERGY_LAMPS; k++) {
        if (!isfinite(s_energy_blob.stats.p100_mw[k]) || s_energy_blob.stats.p100_mw[k] < 0.0f) {
            s_energy_blob.stats.p100_mw[k] = 0.0f;
        }
    }
    *stats = s_energy_blob.stats;
    return ESP_OK;
}

esp_err_t node_store_save_energy(const energy_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_energy_blob.version = ENERGY_VERSION;
    s_energy_blob.stats = *stats;
    return save_blob(KEY_ENERGY, &s_energy_blob, sizeof(s_energy_blob));
}
