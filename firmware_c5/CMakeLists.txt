# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(IDF_TARGET "esp32c5")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/highboy_muse"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/noise_core"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/minimp3"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/argtable3"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/cjson"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/led_strip"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/mdns"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/mqtt"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/littlefs"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/lvgl"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(TentacleOS_C5)

# Compile-only: no assets/temp staging or filesystem image.
