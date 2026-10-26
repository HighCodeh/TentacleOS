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
set(IDF_TARGET "esp32p4")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/firmware_crypto"
    "${CMAKE_CURRENT_LIST_DIR}/components/Applications/gameboy"
    "${CMAKE_CURRENT_LIST_DIR}/components/Applications/doom"
    "${CMAKE_CURRENT_LIST_DIR}/components/Core/resource_mgr"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/fatfs"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/muse_port"
    "${CMAKE_CURRENT_LIST_DIR}/components/Service/script_engine")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Runtime opt-in USB headset; can be compiled out if a release cannot afford it.
option(HIGH_BOY_USB_VOICE "Build experimental High Boy USB audio mode" OFF)
if(HIGH_BOY_USB_VOICE)
    idf_build_set_property(COMPILE_DEFINITIONS "HB_GROK_USB_AUDIO_ENABLED=1" APPEND)
    idf_build_set_property(COMPILE_DEFINITIONS
        "CFG_TUSB_CONFIG_FILE=\"${CMAKE_CURRENT_LIST_DIR}/components/Drivers/grok_usb_audio/include/grok_tusb_config.h\"" APPEND)
endif()

# The -O2 perf build (CONFIG_COMPILER_OPTIMIZATION_PERF) makes GCC run the flow
# analysis behind -Wstringop-truncation / -Wformat-truncation / -Wstringop-overflow
# / -Warray-bounds / -Wmaybe-uninitialized. With the IDF default -Werror=all these
# turn a cascade of pre-existing warnings across unrelated code into hard errors.
# Keep -O2 and -Werror everywhere else; demote just these noisy -O2 analysis
# classes to warnings (appended after IDF's flags so they win). NOTE: some of the
# stringop-overflow / array-bounds hits (LoRa mesh varint/resp buffers) are REAL
# undersized-buffer bugs, not false positives — track them for a dedicated fix.
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format-truncation" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-overflow" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=array-bounds" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=maybe-uninitialized" APPEND)

project(TentacleOS_P4)

# IDF 6 no longer supplies heap declarations through the SPI driver headers.
idf_component_get_property(LED_STRIP_LIB espressif__led_strip COMPONENT_LIB)
target_compile_options(${LED_STRIP_LIB} PRIVATE -include esp_heap_caps.h)

include("${CMAKE_CURRENT_LIST_DIR}/../tools/cmake/highboy_console.cmake")

# APP-only: assets, SD, c5fw and boot metadata are untouched.

# Compact LVGL control/layout code while retaining optimized rasterizers.
idf_component_get_property(HIGHBOY_LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${HIGHBOY_LVGL_LIB} PRIVATE -Os -flto=2 -ffat-lto-objects)
idf_component_get_property(HIGHBOY_LVGL_DIR lvgl__lvgl COMPONENT_DIR)
file(GLOB_RECURSE HIGHBOY_LVGL_DRAW "${HIGHBOY_LVGL_DIR}/src/draw/*.c")
set_source_files_properties(${HIGHBOY_LVGL_DRAW}
    DIRECTORY "${HIGHBOY_LVGL_DIR}" PROPERTIES COMPILE_OPTIONS "-O2")

# Shared script ABI marshalling must fit the paired OTA slots.
idf_component_get_property(HIGHBOY_CJSON_LIB espressif__cjson COMPONENT_LIB)
target_compile_options(${HIGHBOY_CJSON_LIB} PRIVATE -Os -flto=2 -ffat-lto-objects)

# Keep the shared ABI and interpreter adapters in one LTO optimization unit.
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE -flto-partition=one)

# Compact portable cryptographic routines without changing their implementations.
idf_component_get_property(HIGHBOY_SODIUM_LIB espressif__libsodium COMPONENT_LIB)
target_compile_options(${HIGHBOY_SODIUM_LIB} PRIVATE -Os -flto=2 -ffat-lto-objects)
# Keep wiping and constant-time utility calls opaque to whole-program optimization.
idf_component_get_property(HIGHBOY_SODIUM_DIR espressif__libsodium COMPONENT_DIR)
set_property(SOURCE "${HIGHBOY_SODIUM_DIR}/libsodium/src/libsodium/sodium/utils.c"
    DIRECTORY "${HIGHBOY_SODIUM_DIR}"
    APPEND PROPERTY COMPILE_OPTIONS -fno-lto)

# Compact system control and arbitration without changing radio timing paths.
foreach(HIGHBOY_CONTROL_COMPONENT Core resource_mgr console espressif__argtable3 nvs_flash esp_partition vfs wear_levelling app_update esp_app_format)
    idf_component_get_property(HIGHBOY_CONTROL_LIB ${HIGHBOY_CONTROL_COMPONENT} COMPONENT_LIB)
    target_compile_options(${HIGHBOY_CONTROL_LIB} PRIVATE -Os -flto=2 -ffat-lto-objects)
endforeach()

# A factory security profile compacts SDK code while preserving peripheral policy.
if(CONFIG_SECURE_BOOT AND CONFIG_SECURE_FLASH_ENC_ENABLED)
    idf_component_get_property(HIGHBOY_SECURE_DRIVERS Drivers COMPONENT_LIB)
    target_compile_options(${HIGHBOY_SECURE_DRIVERS} PRIVATE -O2)
endif()
