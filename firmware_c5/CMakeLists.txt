# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(IDF_TARGET "esp32c5")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(TentacleOS_C5)

# The C5 is headless: its assets are plain config/html/storage files (no PNG
# icons or fonts), so there is no png->bin conversion step like the P4 has. The
# assets/ tree is staged into temp/ so the build can stamp the current version
# into firmware.json before imaging, without dirtying the source tree.
# The `storage` partition holds runtime captures (evil twin passwords, pcaps,
# GATT dumps, espnow chat) and is formatted on first boot (VFS_FORMAT_ON_FAIL).
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_LIST_DIR}/assets" "${CMAKE_CURRENT_LIST_DIR}/temp"
)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../common/metadata/version_info.txt" FW_VERSION LIMIT_COUNT 1)
string(STRIP "${FW_VERSION}" FW_VERSION)
set(FW_JSON "${CMAKE_CURRENT_LIST_DIR}/temp/config/OTA/firmware.json")
if(EXISTS "${FW_JSON}")
    file(READ "${FW_JSON}" _fw_json)
    string(REGEX REPLACE "\"version\"[ \t]*:[ \t]*\"[^\"]*\"" "\"version\": \"${FW_VERSION}\"" _fw_json "${_fw_json}")
    file(WRITE "${FW_JSON}" "${_fw_json}")
endif()

littlefs_create_partition_image(assets temp FLASH_IN_PROJECT)