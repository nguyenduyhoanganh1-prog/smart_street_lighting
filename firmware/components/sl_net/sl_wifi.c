/**
 * @file sl_wifi.c
 * @brief WiFi station with automatic reconnect (see sl_wifi.h).
 */
#include "sl_wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG           "sl_wifi"
#define BIT_CONNECTED BIT0
#define SSID_MAX_LEN  32U
#define PASS_MAX_LEN  64U
#define RETRY_MIN_MS  1000U  /* first reconnect delay */
#define RETRY_MAX_MS  30000U /* backoff cap (wrong password / AP off) */

static StaticEventGroup_t s_events_buf;
static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry_timer;
static uint32_t s_retry_ms = RETRY_MIN_MS;

static void on_retry_timer(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        xEventGroupClearBits(s_events, BIT_CONNECTED);
        /* Common reasons: 201 = SSID not found (5 GHz?), 15/204 = wrong password */
        ESP_LOGW(TAG, "disconnected (reason %d), retry in %u ms", (int)d->reason,
                 (unsigned)s_retry_ms);
        /* Retry from a timer: never block the default event loop with a delay */
        (void)esp_timer_start_once(s_retry_timer, (uint64_t)s_retry_ms * 1000U);
        s_retry_ms = (s_retry_ms * 2U > RETRY_MAX_MS) ? RETRY_MAX_MS : s_retry_ms * 2U;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_ms = RETRY_MIN_MS;
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

esp_err_t sl_wifi_start(const char *ssid, const char *pass)
{
    if (ssid == NULL || pass == NULL || ssid[0] == '\0'
        || strlen(ssid) > SSID_MAX_LEN || strlen(pass) > PASS_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_events != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_events = xEventGroupCreateStatic(&s_events_buf);
    const esp_timer_create_args_t retry_args = {
        .callback = on_retry_timer,
        .name = "wifi_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &s_retry_timer), TAG, "retry timer");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    const esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) { /* already created is fine */
        return loop_err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }
    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            on_wifi_event, NULL, NULL),
                        TAG, "register wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            on_wifi_event, NULL, NULL),
                        TAG, "register ip handler");

    wifi_config_t wifi_cfg = { 0 };
    memcpy(wifi_cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_cfg.sta.password, pass, strlen(pass));
    wifi_cfg.sta.threshold.authmode = (pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    return ESP_OK;
}

bool sl_wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_events == NULL) {
        return false;
    }
    const EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED, pdFALSE, pdTRUE,
                                                 pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_CONNECTED) != 0U;
}

bool sl_wifi_is_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & BIT_CONNECTED) != 0U;
}
