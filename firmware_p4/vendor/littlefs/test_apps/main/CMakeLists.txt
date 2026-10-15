set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../test")

set(MAIN_TEST_SRCS
    "test_main.c"
    "${TEST_DIR}/test_littlefs.c"
    "${TEST_DIR}/test_littlefs_common.c"
    "${TEST_DIR}/test_dir.c"
    "${TEST_DIR}/test_benchmark.c"
    "${TEST_DIR}/test_littlefs_static_partition.c"
)

set(MAIN_TEST_REQUIRES
    esp_littlefs
    unity
    test_utils
    vfs
    spi_flash
    spiffs
    fatfs
    wear_levelling
    esp_timer
    esp_partition
)

if(NOT IDF_TARGET STREQUAL "esp8266" AND "${IDF_VERSION_MAJOR}" VERSION_GREATER_EQUAL "6")
    list(APPEND MAIN_TEST_SRCS "${TEST_DIR}/test_littlefs_blockdev.c")
    list(APPEND MAIN_TEST_REQUIRES esp_blockdev)
endif()

idf_component_register(
    SRCS ${MAIN_TEST_SRCS}
    INCLUDE_DIRS "${TEST_DIR}"
    PRIV_REQUIRES ${MAIN_TEST_REQUIRES}
    WHOLE_ARCHIVE
)

# Embed the test filesystem binary
target_add_binary_data(${COMPONENT_LIB} "${TEST_DIR}/testfs.bin" BINARY)
