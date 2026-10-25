/**
 * @file wifi_ap.c
 * @brief WiFi AP (WPA2, 192.168.4.1, DHCP server on) plus an optional station that joins
 *        the user's router. AP+STA lets the phone keep using the tuning page while the
 *        board talks to the MQTT broker over the Internet.
 *
 * Note: in AP+STA mode the AP follows the router's channel, so TUNE_AP_CHANNEL is only
 * used when no router is configured.
 */
#include "wifi_ap.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "tune_config.h"

#define TAG "wifi_ap"

#define RETRY_MIN_MS 2000U  /* first reconnect delay */
#define RETRY_MAX_MS 30000U /* backoff cap: every failed scan briefly disturbs the AP */

static atomic_bool s_sta_connected;
static esp_timer_handle_t s_retry_timer;
static uint32_t s_retry_ms = RETRY_MIN_MS;

static void on_retry_timer(void *arg)
{
    (void)arg;
    (void)esp_wifi_connect();
}

static void on_sta_disconnected(const wifi_event_sta_disconnected_t *d)
{
    atomic_store(&s_sta_connected, false);
    /* Common reasons: 201 = SSID not found (5 GHz router?), 15/204 = wrong password */
    ESP_LOGW(TAG, "router link down (reason %d), retry in %u ms", (int)d->reason, (unsigned)s_retry_ms);
    /* Retry from a timer: never block the default event loop with a delay */
    (void)esp_timer_start_once(s_retry_timer, (uint64_t)s_retry_ms * 1000U);
    s_retry_ms = (s_retry_ms * 2U > RETRY_MAX_MS) ? RETRY_MAX_MS : s_retry_ms * 2U;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "router link up, IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_ms = RETRY_MIN_MS;
        atomic_store(&s_sta_connected, true);
    } else if (base != WIFI_EVENT) {
        return;
    } else if (id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        on_sta_disconnected((const wifi_event_sta_disconnected_t *)data);
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *ev = (const wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "client " MACSTR " joined (aid %d)", MAC2STR(ev->mac), ev->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *ev = (const wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "client " MACSTR " left (aid %d)", MAC2STR(ev->mac), ev->aid);
    }
}

bool wifi_sta_is_connected(void)
{
    return atomic_load(&s_sta_connected);
}

/* Fill the station config; returns false if the SSID/password are unusable. */
static bool fill_sta_config(wifi_config_t *sta, const char *ssid, const char *pass)
{
    if (strlen(ssid) >= sizeof(sta->sta.ssid) || strlen(pass) >= sizeof(sta->sta.password)) {
        return false;
    }
    strlcpy((char *)sta->sta.ssid, ssid, sizeof(sta->sta.ssid));
    strlcpy((char *)sta->sta.password, pass, sizeof(sta->sta.password));
    sta->sta.threshold.authmode = (pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    return true;
}

esp_err_t wifi_ap_start(const char *sta_ssid, const char *sta_pass)
{
    const bool use_sta = (sta_ssid != NULL && sta_ssid[0] != '\0');
    wifi_config_t sta = { 0 };
    if (use_sta && !fill_sta_config(&sta, sta_ssid, sta_pass != NULL ? sta_pass : "")) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { /* INVALID_STATE = loop already exists */
        return err;
    }
    (void)esp_netif_create_default_wifi_ap();
    if (use_sta) {
        if (esp_netif_create_default_wifi_sta() == NULL) {
            return ESP_FAIL;
        }
        const esp_timer_create_args_t retry_args = { .callback = on_retry_timer, .name = "wifi_retry" };
        err = esp_timer_create(&retry_args, &s_retry_timer);
        if (err != ESP_OK) {
            return err;
        }
    }

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);
    }
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, TUNE_AP_SSID, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, TUNE_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.ssid_len = (uint8_t)strlen(TUNE_AP_SSID);
    ap.ap.channel = TUNE_AP_CHANNEL;
    ap.ap.max_connection = TUNE_AP_MAX_CLIENTS;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_mode(use_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK && use_sta) {
        err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start(); /* STA_START event then triggers esp_wifi_connect() */
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "AP '%s' up (password '%s') -> open http://" TUNE_AP_IP_TEXT, TUNE_AP_SSID,
                 TUNE_AP_PASSWORD);
        if (use_sta) {
            ESP_LOGI(TAG, "joining router '%s' for MQTT/SNTP", sta_ssid);
        }
    }
    return err;
}
