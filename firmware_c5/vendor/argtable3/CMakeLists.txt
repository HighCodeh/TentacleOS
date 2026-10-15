# This creates a virtual include file with an "argtable3" subdirectory where
# "/argtable3/src/argtable3.h" is copied. the virtual directory is then used as
# the include folder. This is so we can keep including argtable3 header as follow:
# #include "argtable3/argtable3.h"
set(VIRTUAL_INCLUDE_DIR "${CMAKE_BINARY_DIR}/virtual_include")
file(MAKE_DIRECTORY "${VIRTUAL_INCLUDE_DIR}/argtable3")
file(COPY "${COMPONENT_PATH}/argtable3/src/argtable3.h"
     DESTINATION "${VIRTUAL_INCLUDE_DIR}/argtable3")

# list of C files under argtable3 source directory
file(GLOB iec_argtable3_srcs
    "${COMPONENT_PATH}/argtable3/src/*.c"
)

idf_component_register(
    SRCS ${iec_argtable3_srcs}
    INCLUDE_DIRS "${VIRTUAL_INCLUDE_DIR}"
)

# Check if console component is in the build and has argtable3 sources.
# If so, exclude console's argtable3 sources from compilation and make console
# link against this managed argtable3 component instead.
idf_build_get_property(build_components BUILD_COMPONENTS)

if("console" IN_LIST build_components)
    # Get console component directory
    idf_component_get_property(console_dir console COMPONENT_DIR)

    # Check if console still has argtable3 sources (future-proofing)
    if(EXISTS "${console_dir}/argtable3")

        # List of argtable3 source files in console
        file(GLOB console_argtable3_srcs
            "${console_dir}/argtable3/*.c"
        )

        # Exclude these files from compilation by marking them as header-only
        set_source_files_properties(${console_argtable3_srcs}
            DIRECTORY ${console_dir}
            PROPERTIES HEADER_FILE_ONLY ON)

        # Make console link against this managed argtable3 component
        idf_component_add_link_dependency(FROM console)
    endif()
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ARG_ENABLE_LOG=0
        ARG_REPLACE_GETOPT=0
)
