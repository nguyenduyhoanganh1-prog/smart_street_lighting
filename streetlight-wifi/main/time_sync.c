/**
 * @file time_sync.c
 * @brief SNTP time sync and Vietnam time zone (see time_sync.h).
 */
#include "time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

#define TAG "time_sync"

#define SNTP_SERVER        "pool.ntp.org"
#define TZ_VIETNAM         "ICT-7"      /* POSIX TZ: UTC+7 (sign is inverted in POSIX) */
#define FIRST_VALID_EPOCH  1704067200LL /* 2024-01-01: earlier means "not synced yet" */

static bool s_started;

void time_sync_set_timezone(void)
{
    setenv("TZ", TZ_VIETNAM, 1);
    tzset();
}

esp_err_t time_sync_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    /* One attempt only: a failing init would otherwise log every control cycle.
     * Once started, SNTP keeps re-syncing by itself. */
    s_started = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
    const esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SNTP started (%s)", SNTP_SERVER);
    } else {
        ESP_LOGE(TAG, "SNTP init failed: %s (lights fall back to 'no time' rules)",
                 esp_err_to_name(err));
    }
    return err;
}

bool time_sync_is_valid(void)
{
    return (long long)time(NULL) >= FIRST_VALID_EPOCH;
}
