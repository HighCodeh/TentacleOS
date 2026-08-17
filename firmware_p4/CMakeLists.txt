# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(IDF_TARGET "esp32p4")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(TentacleOS_P4)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    # Windows Detection
    message(STATUS "Host System: Windows. Using PowerShell script.")
    set(CONVERT_SCRIPT "${CMAKE_SOURCE_DIR}/../tools/png_to_bin/png_conversor_to_bin.ps1")
    set(CONVERT_COMMAND powershell.exe -ExecutionPolicy Bypass -File "${CONVERT_SCRIPT}")
    
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # Linux or macOS Detection
    message(STATUS "Host System: ${CMAKE_HOST_SYSTEM_NAME}. Using Bash script.")
    set(CONVERT_SCRIPT "${CMAKE_SOURCE_DIR}/../tools/png_to_bin/png_conversor_to_bin.sh")
    set(CONVERT_COMMAND bash "${CONVERT_SCRIPT}")
    
else()
    message(FATAL_ERROR "Unsupported Operating System: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

# Execute the conversion process
execute_process(
    COMMAND ${CONVERT_COMMAND}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE convert_result
)

if(NOT convert_result EQUAL 0)
    message(FATAL_ERROR "Failed to convert assets. Check the logs above for details.")
endif()

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../common/metadata/version_info.txt" FW_VERSION LIMIT_COUNT 1)
string(STRIP "${FW_VERSION}" FW_VERSION)
set(FW_JSON "${CMAKE_CURRENT_LIST_DIR}/temp/config/OTA/firmware.json")
if(EXISTS "${FW_JSON}")
    file(READ "${FW_JSON}" _fw_json)
    string(REGEX REPLACE "\"version\"[ \t]*:[ \t]*\"[^\"]*\"" "\"version\": \"${FW_VERSION}\"" _fw_json "${_fw_json}")
    file(WRITE "${FW_JSON}" "${_fw_json}")
endif()

littlefs_create_partition_image(assets temp FLASH_IN_PROJECT)