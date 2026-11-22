# Road calibration (find the road in a lit frame, cut it into 4 zones). Pure C, no ESP-IDF calls,
# so the same sources are unit-tested on the PC (ai_stream/test/host, firmware/streetlight-node/test/host).
idf_component_register(SRCS "road_zones.c" INCLUDE_DIRS "include")
