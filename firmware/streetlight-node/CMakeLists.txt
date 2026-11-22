# streetlight-node: the single ESP-IDF app of level 1 (ESP32-S3 CAM)
cmake_minimum_required(VERSION 3.16)

# Shared components: sl_net (WiFi + MQTT), bh1750, ina219, lamp_pwm, road_zones.
# edge_impulse (the FOMO model + SDK, ~26 MB) is not copied: the single copy lives in ai_stream.
set(EXTRA_COMPONENT_DIRS ../components ../../ai_stream/components/edge_impulse)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(streetlight-node)
