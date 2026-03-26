idf_component_register()

# Override options defined in freetype/CMakeLists.txt.
# We could have used normal set(...) here if freetype enabled CMake policy CMP0077.
option(FT_DISABLE_HARFBUZZ "" ON)
option(FT_DISABLE_BZIP2 "" ON)
option(FT_DISABLE_BROTLI "" ON)
option(FT_DISABLE_PNG "" ON)
option(FT_DISABLE_ZLIB "" ON)

# These are regular CMake variables, so we can set them directly.
set(SKIP_INSTALL_ALL TRUE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(freetype output)

# https://gitlab.freedesktop.org/freetype/freetype/-/issues/1299
target_compile_options(freetype PRIVATE "-Wno-dangling-pointer")

target_link_libraries(${COMPONENT_LIB} INTERFACE freetype)
