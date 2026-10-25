# sl_codec: JSON encode/decode + config validation of the streetlight protocol
# (same file as firmware/components/sl_net/sl_mqtt_codec.c, without WiFi-STA/MQTT)
idf_component_register(
    SRCS "sl_mqtt_codec.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES json
)
