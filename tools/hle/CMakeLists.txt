cmake_minimum_required(VERSION 3.16)
project(TentacleOS_HLE C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FIRMWARE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(HLE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(HLE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(HLE_ASSETS_DIR "${FIRMWARE_ROOT}/firmware_p4/temp")

# ── Collect firmware component include dirs ───────────────────────────────────
function(collect_firmware_includes TARGET_DIR OUTPUT_VAR)
    set(_includes "")
    file(GLOB_RECURSE _dirs CONFIGURE_DEPENDS LIST_DIRECTORIES true
        "${TARGET_DIR}/components/*/include")
    foreach(_dir ${_dirs})
        list(APPEND _includes ${_dir})
    endforeach()
    set(${OUTPUT_VAR} ${_includes} PARENT_SCOPE)
endfunction()
collect_firmware_includes("${FIRMWARE_ROOT}/firmware_c5" C5_INCLUDES)
collect_firmware_includes("${FIRMWARE_ROOT}/firmware_p4" P4_INCLUDES)

function(collect_firmware_sources TARGET_DIR PATTERN OUTPUT_VAR)
    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS "${TARGET_DIR}/${PATTERN}")
    set(${OUTPUT_VAR} ${_sources} PARENT_SCOPE)
endfunction()

# ── C5 firmware sources ──────────────────────────────────────────────────────
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_c5" "components/Core/*.c" C5_CORE_SRC)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_c5" "main/*.c" C5_MAIN_SRC)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_c5" "components/Drivers/*.c" C5_DRV)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_c5" "components/Service/*.c" C5_SVC)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_c5" "components/Drivers/*/*.c" C5_DRV_DEEP)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_c5" "components/Service/*/*.c" C5_SVC_DEEP)

# ── P4 firmware sources ──────────────────────────────────────────────────────
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Core/*.c" P4_CORE_SRC)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "main/*.c" P4_MAIN_SRC)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Drivers/*.c" P4_DRV)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Drivers/*/*.c" P4_DRV_DEEP)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Service/*.c" P4_SVC)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Service/*/*.c" P4_SVC_DEEP)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Applications/*.c" P4_APP)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Applications/*/*.c" P4_APP_DEEP)
collect_firmware_sources("${FIRMWARE_ROOT}/firmware_p4" "components/Applications/*/*/*.c" P4_APP_DEEP2)

# ── Dependencies ─────────────────────────────────────────────────────────────
include(FetchContent)

file(GLOB_RECURSE HLE_ASSET_SOURCES CONFIGURE_DEPENDS "${FIRMWARE_ROOT}/firmware_p4/assets/*")
set(HLE_ASSETS_STAMP "${HLE_ASSETS_DIR}/.hle_assets_stamp")
add_custom_command(
    OUTPUT ${HLE_ASSETS_STAMP}
    COMMAND bash "${FIRMWARE_ROOT}/tools/png_to_bin/png_conversor_to_bin.sh"
    COMMAND ${CMAKE_COMMAND} -E touch ${HLE_ASSETS_STAMP}
    WORKING_DIRECTORY "${FIRMWARE_ROOT}/firmware_p4"
    DEPENDS ${HLE_ASSET_SOURCES} "${FIRMWARE_ROOT}/tools/png_to_bin/png_conversor_to_bin.sh"
    COMMENT "Generating HLE LVGL assets"
)
add_custom_target(hle_assets DEPENDS ${HLE_ASSETS_STAMP})

# cJSON
set(ENABLE_CJSON_TEST OFF CACHE BOOL "Build cJSON tests" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "Create cJSON uninstall target" FORCE)
set(ENABLE_TARGET_EXPORT OFF CACHE BOOL "Export cJSON CMake targets" FORCE)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "Override cJSON library type" FORCE)
set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "Build cJSON as a shared library" FORCE)
FetchContent_Declare(cjson GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git GIT_TAG v1.7.18)
FetchContent_MakeAvailable(cjson)

FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG v1.14.0)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(LV_BUILD_CONF_DIR "${HLE_INCLUDE_DIR}" CACHE PATH "Directory containing the HLE lv_conf.h" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "Build LVGL's internal ThorVG" FORCE)
FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.4.0)
FetchContent_MakeAvailable(lvgl)

option(HLE_BUILD_INTERACTIVE "Build the SDL2 High Boy simulator" ON)
if(HLE_BUILD_INTERACTIVE)
    find_package(SDL2 REQUIRED)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
add_compile_definitions(HLE_BUILD)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
    -Wno-sign-compare -Wno-missing-field-initializers -fno-strict-aliasing)
set(HLE_FS_WRAP_OPTIONS
    -Wl,--wrap=fopen
    -Wl,--wrap=stat
    -Wl,--wrap=opendir
    -Wl,--wrap=mkdir
    -Wl,--wrap=access
    -Wl,--wrap=remove
    -Wl,--wrap=rename
    -Wl,--wrap=unlink
)

# ── Libraries ────────────────────────────────────────────────────────────────
file(GLOB_RECURSE HLE_SRC CONFIGURE_DEPENDS "${HLE_SRC_DIR}/*.cpp")
list(FILTER HLE_SRC EXCLUDE REGEX ".*main\\.cpp$")        # main is for interactive only
list(FILTER HLE_SRC EXCLUDE REGEX ".*lv_port.*\\.cpp$")   # lv_port needs display context
list(FILTER HLE_SRC EXCLUDE REGEX ".*hle_kernel.*\\.cpp$") # kernel needs LVGL + bridge
add_library(hle_shims STATIC ${HLE_SRC})
target_include_directories(hle_shims PUBLIC ${HLE_INCLUDE_DIR} ${P4_INCLUDES})
target_compile_options(hle_shims PRIVATE -Wno-pmf-conversions)
target_compile_definitions(hle_shims PRIVATE HLE_ASSETS_DIR="${HLE_ASSETS_DIR}")

add_library(firmware_c5 STATIC
    ${C5_CORE_SRC} ${C5_MAIN_SRC} ${C5_DRV} ${C5_DRV_DEEP} ${C5_SVC} ${C5_SVC_DEEP}
)

# C5 hardware transports and network services require ESP-IDF/NimBLE/lwIP stacks.
# HLE provides its own in-memory SPI bridge and compiles P4 proxy clients instead.
get_target_property(_c5_srcs firmware_c5 SOURCES)
list(FILTER _c5_srcs EXCLUDE REGEX ".*bluetooth_service\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*wifi_service\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/host_link/host_link_gatt\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/spi_bridge/spi_bridge\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/ota/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/console/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/dns_server/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/esp_now/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/http_server/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/meshtastic/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/meshcore/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/sd_card/.*\\.c$")
list(FILTER _c5_srcs EXCLUDE REGEX ".*/storage_vfs/vfs_sdcard\\.c$")
set_target_properties(firmware_c5 PROPERTIES SOURCES "${_c5_srcs}")
target_include_directories(firmware_c5 SYSTEM PRIVATE ${HLE_INCLUDE_DIR} ${C5_INCLUDES} ${cjson_SOURCE_DIR})
target_compile_options(firmware_c5 PRIVATE -Wno-implicit-function-declaration)

add_library(firmware_p4 STATIC
    ${P4_CORE_SRC} ${P4_MAIN_SRC}
    ${P4_DRV} ${P4_DRV_DEEP} ${P4_SVC} ${P4_SVC_DEEP}
    ${P4_APP} ${P4_APP_DEEP} ${P4_APP_DEEP2}
)
target_include_directories(firmware_p4 SYSTEM PRIVATE ${HLE_INCLUDE_DIR} ${P4_INCLUDES})
target_include_directories(firmware_p4 SYSTEM PRIVATE ${cjson_SOURCE_DIR} ${lvgl_SOURCE_DIR}/src)
target_compile_options(firmware_p4 PRIVATE -Wno-implicit-function-declaration)
target_link_libraries(firmware_p4 PUBLIC lvgl)

# P4: exclude hardware drivers and transports that need deep external stacks
get_target_property(_p4_srcs firmware_p4 SOURCES)
list(FILTER _p4_srcs EXCLUDE REGEX ".*spi_bridge_phy.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*c5_flasher.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*bad_usb.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*LoRa/.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*/Service/host_link/.*\\.c$")
# WiFi application backends (need esp_wifi/lwIP) and their UI screens
list(FILTER _p4_srcs EXCLUDE REGEX ".*/Applications/wifi/.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*/ui/screens/wifi/.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*/ui/screens/connect_wifi/.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*tusb.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*ys_rfid2.*hal.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*/nfc/.*/nfc_crypto\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*/nfc/.*/mf_desfire\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*/nfc/.*/mf_ultralight\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*/nfc/.*/t2t_emu\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*storage_vfs/vfs_sdcard.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*storage_vfs/vfs_spiffs.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*storage_vfs/vfs_littlefs.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*storage_vfs/vfs_ramfs.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*console/.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*ota/.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*sd_card/.*\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*lv_port_disp\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*lv_port_indev\\.c$")
list(FILTER _p4_srcs EXCLUDE REGEX ".*lv_port_touch\\.c$")
set_target_properties(firmware_p4 PROPERTIES SOURCES "${_p4_srcs}")

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
add_executable(hle_tests ${TEST_SOURCES})
target_link_libraries(hle_tests PRIVATE hle_shims GTest::gtest_main pthread)
target_include_directories(hle_tests PRIVATE ${HLE_INCLUDE_DIR})
target_link_options(hle_tests PRIVATE ${HLE_FS_WRAP_OPTIONS})
include(GoogleTest)
gtest_discover_tests(hle_tests)

# ── Interactive ──────────────────────────────────────────────────────────────
if(HLE_BUILD_INTERACTIVE)
    set(INTERACTIVE_SRC
        "${HLE_SRC_DIR}/main.cpp"
        "${HLE_SRC_DIR}/hal/lv_port_disp_hle.cpp"
        "${HLE_SRC_DIR}/hal/lv_port_indev_hle.cpp"
        "${HLE_SRC_DIR}/hal/hle_kernel.cpp"
        "${HLE_SRC_DIR}/hal/hle_stub_system.cpp"
        "${HLE_SRC_DIR}/hal/hle_stub_ui_wifi.cpp"
        "${HLE_SRC_DIR}/hal/hle_stub_dispatchers.cpp"
    )
    add_executable(hle_interactive ${INTERACTIVE_SRC})
    target_include_directories(hle_interactive PRIVATE ${HLE_INCLUDE_DIR})
    target_include_directories(hle_interactive SYSTEM PRIVATE ${P4_INCLUDES} ${lvgl_SOURCE_DIR} ${lvgl_SOURCE_DIR}/src)
    target_compile_definitions(hle_interactive PRIVATE HLE_ASSETS_DIR="${HLE_ASSETS_DIR}")
    target_link_options(hle_interactive PRIVATE ${HLE_FS_WRAP_OPTIONS})
    target_link_libraries(hle_interactive PRIVATE hle_shims firmware_p4 firmware_c5 lvgl SDL2::SDL2 pthread cjson)
    add_dependencies(hle_interactive hle_assets)
    message(STATUS "SDL2 found — hle_interactive enabled")
endif()
