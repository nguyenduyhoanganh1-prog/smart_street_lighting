idf_component_register(
    SRCS "main.c" "camera_cap.c" "wifi_ap.c" "web_server.c" "lamp_ctrl.c" "ai_pipeline.c"
         "detector.cpp"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES edge_impulse road_zones lamp_pwm esp32-camera nvs_flash esp_wifi esp_netif esp_event
                  esp_http_server esp_timer lwip driver json heap
)

# The Edge Impulse headers trigger -Wuninitialized warnings that ESP-IDF turns into errors.
# Relax them for the one file that includes those headers; our own code stays strict.
set_source_files_properties(detector.cpp PROPERTIES COMPILE_OPTIONS
    "-Wno-error=uninitialized;-Wno-error=maybe-uninitialized;-Wno-error=deprecated-declarations;-Wno-error=missing-field-initializers")
