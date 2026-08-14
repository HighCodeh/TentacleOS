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
# icons or fonts), so there is no png->bin conversion step like the P4 has. Image
# the assets/ tree straight into the `assets` LittleFS partition and flash it.
# The `storage` partition holds runtime captures (evil twin passwords, pcaps,
# GATT dumps, espnow chat) and is formatted on first boot (VFS_FORMAT_ON_FAIL).
littlefs_create_partition_image(assets assets FLASH_IN_PROJECT)