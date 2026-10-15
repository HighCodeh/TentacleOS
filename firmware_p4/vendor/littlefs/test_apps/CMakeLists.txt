# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)

# Include test_utils from ESP-IDF tools
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/test_apps/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_littlefs)
