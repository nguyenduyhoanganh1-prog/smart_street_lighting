idf_component_register(
    SRCS "main.c" "sensor_hub.c" "energy_acc.c" "energy_stats.c"
         "lighting.c" "lamp_fault.c" "lux_check.c" "ina_zero.c" "stop_detect.c" "node_store.c" "time_sync.c"
         "camera_cap.c" "detector.cpp" "vision.c" "zone_rule.c"
         "button_click.c" "mode_button.c" "boot_mode.c"
         "log_view.c" "stream_mode.c" "ai_pipeline.c" "lamp_ctrl.c" "web_server.c" "wifi_ap.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES sl_net bh1750 ina219 lamp_pwm road_zones edge_impulse esp32-camera
                  driver nvs_flash esp_timer esp_netif lwip heap
                  esp_wifi esp_event esp_http_server json
)

# The Edge Impulse headers trigger -Wuninitialized warnings that ESP-IDF turns into errors.
# Relax them for the one file that includes those headers; our own code stays strict.
set_source_files_properties(detector.cpp PROPERTIES COMPILE_OPTIONS
    "-Wno-error=uninitialized;-Wno-error=maybe-uninitialized;-Wno-error=deprecated-declarations;-Wno-error=missing-field-initializers")
