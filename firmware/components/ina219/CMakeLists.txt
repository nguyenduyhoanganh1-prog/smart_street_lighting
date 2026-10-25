# ina219: current/voltage monitor driver (ESP-IDF v5.x, i2c_master API)
idf_component_register(
    SRCS "ina219.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES esp_timer freertos
)
