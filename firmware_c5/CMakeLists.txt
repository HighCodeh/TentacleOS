# SPDX-License-Identifier: GPL-3.0-or-later
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

# A factory security profile compacts SDK code while preserving peripheral policy.
if(CONFIG_SECURE_BOOT AND CONFIG_SECURE_FLASH_ENC_ENABLED)
    idf_component_get_property(HIGHBOY_SECURE_DRIVERS Drivers COMPONENT_LIB)
    target_compile_options(${HIGHBOY_SECURE_DRIVERS} PRIVATE -Og)
    idf_component_get_property(HIGHBOY_SECURE_MUSE highboy_muse COMPONENT_LIB)
    target_compile_options(${HIGHBOY_SECURE_MUSE} PRIVATE -Og)
endif()
