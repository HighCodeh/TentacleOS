# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(IDF_TARGET "esp32c5")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(TentacleOS_C5)

# dev-v1 variant: no `assets`/`storage` partitions (lean OTA layout), so the
# asset conversion + littlefs image generation are dropped. Evil Twin / Beacon
# Spam and scan-saving degrade gracefully (their partitions are absent).