cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
project(console_compat)

idf_build_get_property(python PYTHON)
add_custom_target(check_argtable_path ALL
    COMMAND ${python} ${CMAKE_CURRENT_SOURCE_DIR}/check_argtable_path.py
    ${CMAKE_CURRENT_BINARY_DIR}/console_compat.elf
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    $ENV{IDF_PATH}
    "${_CMAKE_TOOLCHAIN_PREFIX}strings"
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/console_compat.elf
)
