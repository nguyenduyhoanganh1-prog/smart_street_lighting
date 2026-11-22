# ai_stream: live camera stream + on-board FOMO detection (Edge Impulse) + lamp control,
# all served from a WiFi web page. Self-contained ESP-IDF app (own access point, no router/MQTT).
# Only the lamp_pwm driver is shared with streetlight-wifi; the Edge Impulse export lives in
# components/edge_impulse.
cmake_minimum_required(VERSION 3.16)

# road_zones is shared with firmware/streetlight-node (one copy of the algorithm)
set(EXTRA_COMPONENT_DIRS ../streetlight-wifi/components ../firmware/components/road_zones)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ai_stream)
