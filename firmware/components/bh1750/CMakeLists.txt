# bh1750: ambient light sensor driver (ESP-IDF v5.x, i2c_master API)
idf_component_register(
    SRCS "bh1750.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES esp_timer freertos
)
