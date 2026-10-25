idf_component_register(
    SRCS "main.c" "node_ctrl.c" "wifi_ap.c" "web_server.c"
         "sensor_hub.c" "energy_acc.c" "energy_stats.c"
         "lighting.c" "node_store.c" "time_sync.c" "mqtt_link.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES sl_codec sl_mqtt bh1750 ina219 lamp_pwm driver nvs_flash esp_timer esp_netif esp_event
                  esp_wifi esp_http_server lwip
)
