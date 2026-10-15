set(UNITY_DIR "$ENV{IDF_PATH}/components/unity")
set(CMOCK_DIR "$ENV{IDF_PATH}/components/cmock")

include_directories(${UNITY_DIR}/unity/src)
add_definitions(-DENABLE_UNIT_TESTS)

# Use ruby command directly instead of looking for ruby.exe
find_program(RUBY_EXECUTABLE ruby)
if(NOT RUBY_EXECUTABLE)
    message(FATAL_ERROR "Ruby is required for CMock but was not found!")
endif()

include(unity/${UNIT_TESTS}/unit_test.cmake)

# Verify headers exist and create mock commands for each
foreach(mock_file ${MOCK_FILES})
    set(header_path "${COMPONENT_DIR}/private_include/${mock_file}.h")
    if(NOT EXISTS ${header_path})
        message(FATAL_ERROR "Cannot find ${mock_file}.h at ${header_path}")
    endif()

    list(APPEND MOCK_OUTPUTS
            ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_${mock_file}.c
            ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_${mock_file}.h
    )

    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_${mock_file}.c
            ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_${mock_file}.h
            COMMAND ${RUBY_EXECUTABLE}
            ${CMOCK_DIR}/CMock/lib/cmock.rb
            -o${CMAKE_CURRENT_SOURCE_DIR}/unity/cmock_config.yml
            ${header_path}
            DEPENDS ${header_path}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating mock for ${mock_file}.h"
    )
endforeach()

# Include CMock headers
include_directories(
        ${CMOCK_DIR}/CMock/src
        ${CMAKE_CURRENT_BINARY_DIR}/mocks
        ${UNITY_DIR}/unity/src
        ${UNITY_DIR}/include
        unity
)

# Create directory for generated mocks
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/mocks)

foreach(mock_file ${MOCK_FILES})
    list(REMOVE_ITEM SOURCES ${MDNS_DIR}/${mock_file}.c)
endforeach()

# Add test-related sources
list(APPEND SOURCES
        ${UNITY_DIR}/unity/src/unity.c
        ${CMOCK_DIR}/CMock/src/cmock.c
        unity/unity_main.c
        unity/create_test_packet.c
)

# Add all generated mock files
foreach(mock_file ${MOCK_FILES})
    list(APPEND SOURCES ${CMAKE_CURRENT_BINARY_DIR}/mocks/mock_${mock_file}.c)
endforeach()
