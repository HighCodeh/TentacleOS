# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

set(TOS_SDMMC_PATCH_DIR "${CMAKE_CURRENT_LIST_DIR}/../../firmware_p4/components/Drivers/sdmmc")
set(TOS_SDMMC_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/../sdmmc/prepare.py")
set(TOS_SDMMC_COMPONENT_DIR "${CMAKE_BINARY_DIR}/components/Drivers/sdmmc")
if(NOT PYTHON)
    find_program(PYTHON NAMES python3 python REQUIRED)
endif()
file(GLOB_RECURSE TOS_SDMMC_SDK_INPUTS "$ENV{IDF_PATH}/components/sdmmc/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${TOS_SDMMC_SDK_INPUTS}
    "$ENV{IDF_PATH}/tools/cmake/version.cmake"
    "${TOS_SDMMC_PATCH_DIR}/patch_manifest.json"
    "${TOS_SDMMC_PATCH_DIR}/patch_manifest.json.license"
    "${TOS_SDMMC_PATCH_DIR}/write_errors.patch"
    "${TOS_SDMMC_GENERATOR}")
execute_process(COMMAND "${PYTHON}" "${TOS_SDMMC_GENERATOR}"
    --idf "$ENV{IDF_PATH}" --out "${TOS_SDMMC_COMPONENT_DIR}"
    RESULT_VARIABLE TOS_SDMMC_STATUS)
if(NOT TOS_SDMMC_STATUS EQUAL 0)
    message(FATAL_ERROR "Cannot generate the reviewed SDMMC override")
endif()
list(APPEND EXTRA_COMPONENT_DIRS "${TOS_SDMMC_COMPONENT_DIR}")
