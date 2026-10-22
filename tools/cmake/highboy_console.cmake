# SPDX-License-Identifier: GPL-3.0-or-later
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

# Keep the pinned SDK immutable; only its console target uses the generated fix.
idf_component_get_property(HIGHBOY_CONSOLE_DIR console COMPONENT_DIR)
idf_component_get_property(HIGHBOY_CONSOLE_LIB console COMPONENT_LIB)
set(HIGHBOY_LINENOISE_ORIGINAL "${HIGHBOY_CONSOLE_DIR}/linenoise/linenoise.c")
set(HIGHBOY_LINENOISE_FIXED "${CMAKE_BINARY_DIR}/highboy_console/linenoise.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${HIGHBOY_LINENOISE_ORIGINAL}"
    "${CMAKE_CURRENT_LIST_DIR}/../console/patch_linenoise.py")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../console/patch_linenoise.py"
    --source "${HIGHBOY_LINENOISE_ORIGINAL}" --out "${HIGHBOY_LINENOISE_FIXED}"
    RESULT_VARIABLE HIGHBOY_CONSOLE_PATCH_STATUS)
if(NOT HIGHBOY_CONSOLE_PATCH_STATUS EQUAL 0)
    message(FATAL_ERROR "The pinned console line-editor patch could not be generated")
endif()
get_target_property(HIGHBOY_CONSOLE_SOURCES ${HIGHBOY_CONSOLE_LIB} SOURCES)
set(HIGHBOY_CONSOLE_PATCH_COUNT 0)
set(HIGHBOY_CONSOLE_FIXED_SOURCES "")
foreach(HIGHBOY_CONSOLE_SOURCE IN LISTS HIGHBOY_CONSOLE_SOURCES)
    if(HIGHBOY_CONSOLE_SOURCE STREQUAL HIGHBOY_LINENOISE_ORIGINAL OR
       HIGHBOY_CONSOLE_SOURCE STREQUAL "linenoise/linenoise.c")
        list(APPEND HIGHBOY_CONSOLE_FIXED_SOURCES "${HIGHBOY_LINENOISE_FIXED}")
        math(EXPR HIGHBOY_CONSOLE_PATCH_COUNT "${HIGHBOY_CONSOLE_PATCH_COUNT} + 1")
    else()
        list(APPEND HIGHBOY_CONSOLE_FIXED_SOURCES "${HIGHBOY_CONSOLE_SOURCE}")
    endif()
endforeach()
if(NOT HIGHBOY_CONSOLE_PATCH_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one linenoise source in the console component")
endif()
set_property(TARGET ${HIGHBOY_CONSOLE_LIB} PROPERTY SOURCES "${HIGHBOY_CONSOLE_FIXED_SOURCES}")
target_include_directories(${HIGHBOY_CONSOLE_LIB} PRIVATE "${HIGHBOY_CONSOLE_DIR}/linenoise")
