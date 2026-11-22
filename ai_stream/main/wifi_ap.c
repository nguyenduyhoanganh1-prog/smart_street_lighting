/**
 * @file wifi_ap.c
 * @brief WiFi access point (WPA2) with the default ESP-IDF AP netif: 192.168.4.1, DHCP server on.
 */
#include "wifi_ap.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "tune_config.h"

#define TAG "wifi_ap"

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *ev = (const wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "client " MACSTR " joined (aid %d)", MAC2STR(ev->mac), ev->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *ev = (const wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "client " MACSTR " left (aid %d)", MAC2STR(ev->mac), ev->aid);
    }
}

esp_err_t wifi_ap_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { /* INVALID_STATE = loop already exists */
        return err;
    }
    (void)esp_netif_create_default_wifi_ap();

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
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

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "AP '%s' up (password '%s') -> open http://" TUNE_AP_IP_TEXT, TUNE_AP_SSID,
                 TUNE_AP_PASSWORD);
    }
    return err;
}
