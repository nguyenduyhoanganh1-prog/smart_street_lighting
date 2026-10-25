# sl_mqtt: MQTT/TLS client (HiveMQ Cloud) + WiFi/MQTT credentials in NVS.
# Same sources as firmware/components/sl_net (sl_mqtt.c, sl_secrets.c); the JSON codec
# comes from the sl_codec component, WiFi is handled by main/wifi_ap.c (AP + STA).
idf_component_register(
    SRCS "sl_mqtt.c" "sl_secrets.c"
    INCLUDE_DIRS "include"
    REQUIRES sl_codec
    PRIV_REQUIRES mqtt esp-tls mbedtls nvs_flash esp_timer
)
