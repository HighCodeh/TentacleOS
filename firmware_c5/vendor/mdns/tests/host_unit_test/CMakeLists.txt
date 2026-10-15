cmake_minimum_required(VERSION 3.5)
if(ESP_PLATFORM)
    # Used for reconfiguration of the project
    # Executed by idf.py reconfigure
    set(EXTRA_COMPONENT_DIRS ../../)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(mdns_host_unit_test_config)
    return()
endif()

project(mdns_host_unit_test C)

# Set ENABLE_UNIT_TESTS with a default of OFF
if(NOT DEFINED UNIT_TESTS)
    set(UNIT_TESTS "OFF" CACHE STRING "Unit tests: OFF, test_receiver, test_sender")
else()
    set(ENABLE_UNIT_TESTS 1)
    message(STATUS "Unit testing enabled with UNIT_TESTS=${UNIT_TESTS}")
endif()

# Set variables for directories
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(STUB_DIR ${TEST_DIR}/stubs)
set(MDNS_DIR ${TEST_DIR}/../../)
set(COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../)
set(ESP_NETIF_LINUX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../host_test/components/esp_netif_linux/include)
set(COMMON_COMPONENTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../common_components/linux_compat/)
set(IDF_COMPONENTS_DIR "$ENV{IDF_PATH}/components")

include_directories(
    ${TEST_DIR}
    ${STUB_DIR}
    ${TEST_DIR}/build/config
    ${COMMON_COMPONENTS_DIR}/freertos/include
    ${COMMON_COMPONENTS_DIR}/esp_timer/include
    ${IDF_COMPONENTS_DIR}/esp_event/include
    ${IDF_COMPONENTS_DIR}/esp_netif/include
    ${IDF_COMPONENTS_DIR}/esp_common/include
    ${IDF_COMPONENTS_DIR}/esp_system/include
    ${IDF_COMPONENTS_DIR}/log/include
    ${IDF_COMPONENTS_DIR}/esp_hw_support/etm/include
    ${IDF_COMPONENTS_DIR}/soc/linux/include
    ${IDF_COMPONENTS_DIR}/esp_rom/include
    ${IDF_COMPONENTS_DIR}/heap/include
    ${IDF_COMPONENTS_DIR}/esp_rom/linux/include/linux/
    ${ESP_NETIF_LINUX_DIR}
    ${COMPONENT_DIR}
    ${COMPONENT_DIR}/include
    ${COMPONENT_DIR}/private_include
)

# Source files
set(SOURCES
    ${STUB_DIR}/mdns_mem_caps.c
    ${STUB_DIR}/esp_idf.c
    ${STUB_DIR}/mdns_networking.c
    ${STUB_DIR}/mdns_engine.c
    ${MDNS_DIR}/mdns_receive.c
    ${MDNS_DIR}/mdns_utils.c
    ${MDNS_DIR}/mdns_browser.c
    ${MDNS_DIR}/mdns_querier.c
    ${MDNS_DIR}/mdns_responder.c
    ${MDNS_DIR}/mdns_send.c
    ${MDNS_DIR}/mdns_pcb.c
    ${MDNS_DIR}/mdns_netif.c
)

if(ENABLE_UNIT_TESTS)
    include(unity/unit_test.cmake)
else()
    list(APPEND SOURCES main.c)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

# Add libbsd if available
find_library(LIB_BSD bsd)
if(LIB_BSD)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIB_BSD})
elseif(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(WARNING "Missing LIBBSD library. Install libbsd-dev package and/or check linker directories.")
endif()

# Enable testing if unit tests are enabled
if(ENABLE_UNIT_TESTS)
    include(unity/enable_testing.cmake)
endif()
