# streetlight-node: the single ESP-IDF app of level 1 (ESP32-S3 CAM)
cmake_minimum_required(VERSION 3.16)

# Shared components: sl_net (WiFi + MQTT), bh1750, ina219
set(EXTRA_COMPONENT_DIRS ../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(streetlight-node)
