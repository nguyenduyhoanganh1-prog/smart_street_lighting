# Logic shared with streetlight-node is compiled from its folder (not copied),
# so a fix there is picked up here too.
set(NODE_MAIN ../../firmware/streetlight-node/main)

idf_component_register(
    SRCS "main.c" "node_ctrl.c" "wifi_ap.c" "web_server.c"
         "${NODE_MAIN}/sensor_hub.c" "${NODE_MAIN}/energy_acc.c" "${NODE_MAIN}/energy_stats.c"
         "${NODE_MAIN}/lighting.c" "${NODE_MAIN}/node_store.c" "${NODE_MAIN}/time_sync.c"
    INCLUDE_DIRS "." "${NODE_MAIN}"
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES sl_net bh1750 ina219 lamp_pwm driver nvs_flash esp_timer esp_netif esp_event
                  esp_wifi esp_http_server lwip
)
