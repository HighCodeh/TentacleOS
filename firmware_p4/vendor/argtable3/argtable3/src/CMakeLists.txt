################################################################################
# This file is part of the argtable3 library.
#
# Copyright (C) 2016-2025 Tom G. Huang
# <tomghuang@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of STEWART HEITMANN nor the  names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL STEWART HEITMANN BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
################################################################################

################################################################################
# Compiler and build options
################################################################################

if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Wall")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /WX")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4820")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4710")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4255")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd5045")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4668")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4711")
else(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c89")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
  if ((CMAKE_C_COMPILER_ID MATCHES "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.2)
    OR CMAKE_C_COMPILER_ID MATCHES "Clang")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpedantic")
  endif()
endif()

if(ARGTABLE3_ENABLE_ARG_REX_DEBUG)
  add_definitions(-DARG_REX_DEBUG)
endif()

if(NOT ARGTABLE3_REPLACE_GETOPT)
  add_definitions(-DARG_REPLACE_GETOPT=0)
  add_definitions(-DREPLACE_GETOPT)
endif()

if(ARGTABLE3_LONG_ONLY)
  add_definitions(-DARG_LONG_ONLY)
endif()

add_definitions(-D_XOPEN_SOURCE=700)

################################################################################
# Library target definition
################################################################################
if(BUILD_SHARED_LIBS AND (CMAKE_C_COMPILER_ID STREQUAL "MSVC"))
  configure_file(
    "${PROJECT_SOURCE_DIR}/src/version.rc.in"
    "${PROJECT_BINARY_DIR}/src/version.rc"
  )
  add_library(argtable3 ${ARGTABLE3_SRC_FILES} "${PROJECT_BINARY_DIR}/src/version.rc")
  target_compile_definitions(argtable3 INTERFACE argtable3_IMPORTS)
else()
  add_library(argtable3 ${ARGTABLE3_SRC_FILES})
endif()

add_library(${ARGTABLE3_PROJECT_NAME}::argtable3 ALIAS argtable3)
target_include_directories(argtable3 PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

set_target_properties(argtable3 PROPERTIES
  VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
  SOVERSION "${PROJECT_VERSION_MAJOR}"
  DEBUG_POSTFIX "d"
  PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/src/argtable3.h"
)

################################################################################
# Install rules
################################################################################

if(UNIX OR MSYS OR MINGW OR PSP)
  set(ARGTABLE3_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/argtable3)
elseif(WIN32)
  set(ARGTABLE3_INSTALL_CMAKEDIR "cmake")
endif()

install(TARGETS argtable3
  EXPORT ${ARGTABLE3_PACKAGE_NAME}Config
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ${ARGTABLE3_PACKAGE_NAME}Config
  NAMESPACE ${ARGTABLE3_PROJECT_NAME}::
  DESTINATION ${ARGTABLE3_INSTALL_CMAKEDIR}
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/${ARGTABLE3_PACKAGE_NAME}ConfigVersion.cmake"
  VERSION ${ARGTABLE3_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES "${PROJECT_BINARY_DIR}/${ARGTABLE3_PACKAGE_NAME}ConfigVersion.cmake"
  DESTINATION ${ARGTABLE3_INSTALL_CMAKEDIR}
)
