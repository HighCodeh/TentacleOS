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

# Managed sources remain untouched; only the pinned translation unit is replaced.
idf_component_get_property(TOS_CDC_LIB espressif__usb_host_cdc_acm COMPONENT_LIB)
idf_component_get_property(TOS_CDC_DIR espressif__usb_host_cdc_acm COMPONENT_DIR)
set(TOS_CDC_PREPARE "${CMAKE_CURRENT_LIST_DIR}/../../../../tools/usb_ethernet/prepare_cdc.py")
set(TOS_CDC_PATCHED "${CMAKE_BINARY_DIR}/components/Drivers/usb_cdc_ncm/cdc_acm_host.c")
execute_process(COMMAND "${PYTHON}" "${TOS_CDC_PREPARE}"
    "${TOS_CDC_DIR}/cdc_acm_host.c" "${TOS_CDC_PATCHED}"
    RESULT_VARIABLE TOS_CDC_RESULT)
if(NOT TOS_CDC_RESULT EQUAL 0)
    message(FATAL_ERROR "CDC-NCM compatibility preparation failed")
endif()
get_target_property(TOS_CDC_SOURCES ${TOS_CDC_LIB} SOURCES)
list(REMOVE_ITEM TOS_CDC_SOURCES "${TOS_CDC_DIR}/cdc_acm_host.c" "cdc_acm_host.c")
list(APPEND TOS_CDC_SOURCES "${TOS_CDC_PATCHED}")
set_property(TARGET ${TOS_CDC_LIB} PROPERTY SOURCES "${TOS_CDC_SOURCES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${TOS_CDC_PREPARE}" "${TOS_CDC_DIR}/cdc_acm_host.c")
