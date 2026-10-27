# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(IDF_TARGET "esp32c5")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/firmware_crypto"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/muse_port"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/noise_core"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/minimp3"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(TentacleOS_C5)

# IDF 6 no longer supplies heap declarations through the SPI driver headers.
idf_component_get_property(LED_STRIP_LIB espressif__led_strip COMPONENT_LIB)
target_compile_options(${LED_STRIP_LIB} PRIVATE -include esp_heap_caps.h)

# Compile-only: no assets/temp staging or filesystem image.

# A factory security profile compacts SDK code while preserving peripheral policy.
if(CONFIG_SECURE_BOOT AND CONFIG_SECURE_FLASH_ENC_ENABLED)
    idf_component_get_property(HIGHBOY_SECURE_DRIVERS Drivers COMPONENT_LIB)
    target_compile_options(${HIGHBOY_SECURE_DRIVERS} PRIVATE -Og)
    idf_component_get_property(HIGHBOY_SECURE_MUSE muse_port COMPONENT_LIB)
    target_compile_options(${HIGHBOY_SECURE_MUSE} PRIVATE -Og)
endif()

# Retain internal RAM headroom as SDK support code grows in IDF 6.1.
foreach(SUPPORT_COMPONENT esp_hw_support esp_libc mbedtls)
    idf_component_get_property(SUPPORT_LIB ${SUPPORT_COMPONENT} COMPONENT_LIB)
    target_compile_options(${SUPPORT_LIB} PRIVATE -Os)
endforeach()
