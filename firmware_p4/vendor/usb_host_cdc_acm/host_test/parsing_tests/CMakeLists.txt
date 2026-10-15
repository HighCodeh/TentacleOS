cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)

# Register usb component, must be registered before registering mock
list(APPEND EXTRA_COMPONENT_DIRS "../../../../../usb")

list(APPEND EXTRA_COMPONENT_DIRS
    "../../../../../usb/test/mocks/usb_host_full_mock/usb"    # Full USB Host stack mock (all the layers are mocked)
     "$ENV{IDF_PATH}/tools/mocks/freertos/"
    )

add_definitions("-DCMOCK_MEM_DYNAMIC")
project(host_test_usb_cdc)
