# ESP-IDF component wrapping the Edge Impulse C++ library exported from Studio
# (project "TKHTN", Impulse #1: FOMO int8, 128x128 grayscale, labels car/human).
#
# The zip from Studio only ships a Zephyr CMake file, so this one is based on Edge Impulse's
# official ESP32 example (example-standalone-inferencing-espressif-esp32) and on the ESP branch
# of edge-impulse-sdk/cmake/zephyr/CMakeLists.txt. To update the model: unzip the new export over
# model-parameters/, tflite-model/ and edge-impulse-sdk/ (do NOT overwrite this file).

set(EI_SDK_FOLDER ${CMAKE_CURRENT_LIST_DIR}/edge-impulse-sdk)

set(EI_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tflite-model
    ${CMAKE_CURRENT_LIST_DIR}/model-parameters
    ${EI_SDK_FOLDER}
    ${EI_SDK_FOLDER}/porting
    ${EI_SDK_FOLDER}/porting/espressif/ESP-NN
    ${EI_SDK_FOLDER}/porting/espressif/ESP-NN/include
    ${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft/include
    ${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/common/include
)

set(EI_SOURCE_FILES "")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(${EI_SDK_FOLDER}/cmake/utils.cmake)

    # Everything except the ARM-only CMSIS tree (ESP-NN / esp-dsp replace it on the ESP32-S3)
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(EI_CPP_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cpp")
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(EI_CC_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cc")
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(EI_C_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.c")
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(EI_MODEL_FILES "${CMAKE_CURRENT_LIST_DIR}/tflite-model" "CMSIS" "*.cpp")
    RECURSIVE_FIND_FILE_APPEND(EI_ASM_FILES "${EI_SDK_FOLDER}/porting/espressif/ESP-NN" "*.s")
    RECURSIVE_FIND_FILE_APPEND(EI_ASM_FILES "${EI_SDK_FOLDER}/porting/espressif/ESP-NN" "*.S")
    RECURSIVE_FIND_FILE_APPEND(EI_ASM_FILES "${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft" "*.s")
    RECURSIVE_FIND_FILE_APPEND(EI_ASM_FILES "${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft" "*.S")

    list(APPEND EI_SOURCE_FILES ${EI_CPP_FILES} ${EI_CC_FILES} ${EI_C_FILES} ${EI_MODEL_FILES} ${EI_ASM_FILES})
    list(REMOVE_DUPLICATES EI_SOURCE_FILES)
endif()

idf_component_register(SRCS ${EI_SOURCE_FILES}
                       INCLUDE_DIRS ${EI_INCLUDE_DIRS}
                       REQUIRES esp_timer esp_system freertos heap)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # PUBLIC: the headers included by main/detector.cpp must see the same switches as the library
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        EIDSP_USE_ESP_DSP=1
        asm=__asm__)                           # needed by the ESP-NN headers

    # ESP-NN kernels: ~50x faster than the reference kernels on the ESP32-S3 (4.8 s -> expect
    # well under 1 s). They need 16-byte aligned scratch buffers: main/detector.cpp overrides
    # ei_calloc() for that. Without the override the first inference corrupts the heap.
    # Switch off with: idf.py -DEI_USE_ESP_NN=OFF reconfigure
    option(EI_USE_ESP_NN "Use ESP-NN optimised TFLite kernels" ON)
    if(EI_USE_ESP_NN)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=1)
        if("${IDF_TARGET}" STREQUAL "esp32s3")
            target_compile_definitions(${COMPONENT_LIB} PUBLIC EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN_S3=1)
        endif()
    endif()

    target_compile_options(${COMPONENT_LIB} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17> -Wno-unused-variable -Wno-maybe-uninitialized -Wno-error=format=
        -Wno-unused-but-set-parameter -Wno-error=nonnull-compare -Wno-error=stringop-truncation
        -Wno-stringop-truncation -Wno-error=uninitialized -Wno-deprecated-declarations
        -fno-if-conversion)
endif()
