# tune-by-wifi: ESP32-S3 opens its own WiFi access point and serves a web page
# (live sensor values + lighting parameters), no router / Internet / MQTT needed.
cmake_minimum_required(VERSION 3.16)

# Reuse the shared components of the main firmware (bh1750, ina219, lamp_pwm, sl_net codec)
set(EXTRA_COMPONENT_DIRS ../firmware/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tune-by-wifi)
