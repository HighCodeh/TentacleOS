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

cmake_minimum_required(VERSION 3.5...3.10)

################################################################################
# Project metadata and build options
################################################################################

set(ARGTABLE3_PROJECT_NAME "argtable3")
set(ARGTABLE3_PACKAGE_NAME "Argtable3")

project(${ARGTABLE3_PROJECT_NAME} "C")
set(PROJECT_DESCRIPTION "ANSI C command-line parsing library")
set(PROJECT_HOMEPAGE_URL "https://www.argtable.org")
set(COMPANY_NAME "The Argtable3 Project")
set(FILE_DESC "${PROJECT_DESCRIPTION}")
set(INTERNAL_NAME "${PROJECT_NAME}")
set(LEGAL_COPYRIGHT "Copyright (C) 1998-2001,2003-2011,2013 Stewart Heitmann. All rights reserved.")
set(ORIGINAL_FILE_NAME "${PROJECT_NAME}")
set(PRODUCT_NAME "${ARGTABLE3_PACKAGE_NAME}")
set(MAINTAINER_CONTACT "tomghuang@gmail.com")

option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(ARGTABLE3_ENABLE_TESTS "Enable unit tests" ON)
option(ARGTABLE3_ENABLE_EXAMPLES "Enable examples" ON)
option(ARGTABLE3_ENABLE_ARG_REX_DEBUG "Enable arg_rex debug output" OFF)
option(ARGTABLE3_REPLACE_GETOPT "Replace getopt in the system C library" ON)
option(ARGTABLE3_LONG_ONLY "Use getopt_long_only instead of getopt_long" OFF)

include(GNUInstallDirs)

################################################################################
# Version handling: read from version.tag if present, otherwise use defaults.
# version.tag format: major.minor.patch.build (e.g., 3.2.1.0)
################################################################################

function(safe_parse_semver VERSION_STRING)
  set(MAJOR "")
  set(MINOR "")
  set(PATCH "")
  set(PRERELEASE "")
  set(BUILD "")

  foreach(i RANGE 0 9)
    unset(CMAKE_MATCH_${i})
  endforeach()

  set(SEMVER_REGEX "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([A-Za-z0-9.-]+))?(\\+([A-Za-z0-9.-]+))?$")
  if(VERSION_STRING MATCHES ${SEMVER_REGEX})
    set(MAJOR ${CMAKE_MATCH_1})
    set(MINOR ${CMAKE_MATCH_2})
    set(PATCH ${CMAKE_MATCH_3})

    if(DEFINED CMAKE_MATCH_5 AND NOT CMAKE_MATCH_5 STREQUAL "")
      set(PRERELEASE ${CMAKE_MATCH_5})
    endif()

    if(DEFINED CMAKE_MATCH_7 AND NOT CMAKE_MATCH_7 STREQUAL "")
      set(BUILD ${CMAKE_MATCH_7})
    endif()

    set(SEMVER_MAJOR ${MAJOR} PARENT_SCOPE)
    set(SEMVER_MINOR ${MINOR} PARENT_SCOPE)
    set(SEMVER_PATCH ${PATCH} PARENT_SCOPE)
    set(SEMVER_PRERELEASE ${PRERELEASE} PARENT_SCOPE)
    set(SEMVER_BUILD ${BUILD} PARENT_SCOPE)
    set(SEMVER_VALID TRUE PARENT_SCOPE)
  else()
    set(SEMVER_VALID FALSE PARENT_SCOPE)
    message(FATAL_ERROR "Invalid semver format: ${VERSION_STRING}")
  endif()
endfunction()

get_filename_component(VERSION_TAG_PATH "version.tag" ABSOLUTE)
set(VERSION_TAG "0.0.0") # Default version if no tag file exists.
if(EXISTS "${VERSION_TAG_PATH}")
  file(READ "${VERSION_TAG_PATH}" VERSION_TAG OFFSET 1) # Skip the first "v" character.
endif()
safe_parse_semver("${VERSION_TAG}")

set(PROJECT_VERSION_MAJOR ${SEMVER_MAJOR})
set(PROJECT_VERSION_MINOR ${SEMVER_MINOR})
set(PROJECT_VERSION_PATCH ${SEMVER_PATCH})
set(PROJECT_VERSION_PRERELEASE ${SEMVER_PRERELEASE})
set(PROJECT_VERSION_BUILD ${SEMVER_BUILD})
set(PROJECT_VERSION_TWEAK 0)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}")
set(ARGTABLE3_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(ARGTABLE3_FULL_VERSION "${VERSION_TAG}")

################################################################################
# Source files for the argtable3 library
################################################################################

set(ARGTABLE3_AMALGAMATION_SRC_FILE ${PROJECT_SOURCE_DIR}/dist/argtable3.c)
set(ARGTABLE3_SRC_FILES
  ${PROJECT_SOURCE_DIR}/src/arg_cmd.c
  ${PROJECT_SOURCE_DIR}/src/arg_date.c
  ${PROJECT_SOURCE_DIR}/src/arg_dbl.c
  ${PROJECT_SOURCE_DIR}/src/arg_dstr.c
  ${PROJECT_SOURCE_DIR}/src/arg_end.c
  ${PROJECT_SOURCE_DIR}/src/arg_file.c
  ${PROJECT_SOURCE_DIR}/src/arg_hashtable.c
  ${PROJECT_SOURCE_DIR}/src/arg_int.c
  ${PROJECT_SOURCE_DIR}/src/arg_lit.c
  ${PROJECT_SOURCE_DIR}/src/arg_rem.c
  ${PROJECT_SOURCE_DIR}/src/arg_rex.c
  ${PROJECT_SOURCE_DIR}/src/arg_str.c
  ${PROJECT_SOURCE_DIR}/src/arg_utils.c
  ${PROJECT_SOURCE_DIR}/src/argtable3.c
  ${PROJECT_SOURCE_DIR}/src/arg_getopt_long.c
)

################################################################################
# Packaging configuration (CPack)
################################################################################

set(PackagingTemplatesDir "${CMAKE_CURRENT_SOURCE_DIR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

################################################################################
# Platform-specific settings
################################################################################

if(APPLE)
  set(CMAKE_MACOSX_RPATH ON)
endif()

################################################################################
# Add subdirectories for library, examples, and tests
################################################################################

add_subdirectory(src)

if(ARGTABLE3_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

if(ARGTABLE3_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

################################################################################
# Pkg-config file generation and installation
################################################################################

set(PKG_CONFIG_FILE_NAME "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.pc")
configure_file("${PackagingTemplatesDir}/pkgconfig.pc.in" "${PKG_CONFIG_FILE_NAME}" @ONLY)
install(FILES "${PKG_CONFIG_FILE_NAME}"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

################################################################################
# Packaging (CPack) configuration
################################################################################

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${ARGTABLE3_VERSION}")
set(CPACK_PACKAGE_VENDOR "${COMPANY_NAME}")
set(CPACK_PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_PACKAGE_CONTACT "${MAINTAINER_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CPACK_RESOURCE_FILE_README}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${CPACK_PACKAGE_DESCRIPTION}")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_NAME "lib${PROJECT_NAME}-dev")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6-dev")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "cmake, pkg-config")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")

set(CPACK_RPM_PACKAGE_NAME "lib${PROJECT_NAME}-devel")
set(CPACK_RPM_PACKAGE_SUGGESTS "${CPACK_DEBIAN_PACKAGE_SUGGESTS}")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_RELEASE 1)

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_NSIS_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")

################################################################################
# Enable CPack
################################################################################

include(CPack)
