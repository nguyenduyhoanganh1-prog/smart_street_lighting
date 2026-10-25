# lamp_pwm: 4-channel LEDC dimming with hardware fades (ESP-IDF v5.x)
idf_component_register(
    SRCS "lamp_pwm.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_timer
)
