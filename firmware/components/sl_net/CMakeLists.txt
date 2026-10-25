# sl_net: WiFi + MQTT/TLS (HiveMQ Cloud) + credentials in NVS
# Target: ESP-IDF v5.x (cJSON is the "json" component, esp-mqtt is "mqtt")
idf_component_register(
    SRCS "sl_wifi.c" "sl_secrets.c" "sl_mqtt.c" "sl_mqtt_codec.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt json mbedtls esp_timer
)
