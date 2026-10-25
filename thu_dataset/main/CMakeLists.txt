idf_component_register(
    SRCS "main.c" "camera_cap.c" "sd_store.c" "capture_ctrl.c" "dataset_naming.c" "gray_image.c"
         "wifi_ap.c" "web_server.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES lamp_pwm esp32-camera nvs_flash esp_wifi esp_netif esp_event esp_http_server
                  esp_timer lwip fatfs sdmmc vfs driver json
)
