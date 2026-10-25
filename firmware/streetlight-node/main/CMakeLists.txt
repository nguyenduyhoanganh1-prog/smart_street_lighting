idf_component_register(
    SRCS "main.c" "sensor_hub.c" "energy_acc.c" "energy_stats.c"
         "lighting.c" "node_store.c" "time_sync.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES sl_net bh1750 ina219 lamp_pwm driver nvs_flash esp_timer esp_netif lwip
)
