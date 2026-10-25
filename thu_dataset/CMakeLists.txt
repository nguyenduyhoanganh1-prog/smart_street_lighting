# thu_dataset: capture 640x360 camera frames to an SD card, controlled from a WiFi web page.
# Self-contained ESP-IDF app (own WiFi access point, no router, no MQTT).
# Only the lamp_pwm driver is shared with streetlight-wifi; the other components found in that
# folder (bh1750, ina219, sl_codec) are not required by main, so they are not built.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ../streetlight-wifi/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(thu_dataset)
