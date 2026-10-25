# streetlight-wifi: self-contained street-light node without AI/camera.
# The ESP32-S3 opens its own WiFi access point and serves a dashboard-like web page
# (live sensor values + lighting parameters). No router, Internet or MQTT needed.
cmake_minimum_required(VERSION 3.16)

# Local components: bh1750, ina219, lamp_pwm, sl_codec
set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(streetlight-wifi)
