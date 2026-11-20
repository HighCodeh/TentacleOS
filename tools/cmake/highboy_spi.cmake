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

# Scoped IDF 6.1 exception: preserve upstream sources and patch only OOM cleanup.
set(TOS_SPI_SOURCE "$ENV{IDF_PATH}/components/esp_driver_spi")
set(TOS_SPI_OVERRIDE "${CMAKE_BINARY_DIR}/components/Drivers/esp_driver_spi")
file(SHA256 "${TOS_SPI_SOURCE}/src/gpspi/spi_master.c" TOS_SPI_HASH)
if(NOT TOS_SPI_HASH STREQUAL "51bdac6264b48c8215e768fdd46a2e7708d44257177455d44665bf1a857654e1")
    message(FATAL_ERROR "SPI SDK source changed; review the OOM cleanup override")
endif()
file(COPY "${TOS_SPI_SOURCE}/" DESTINATION "${TOS_SPI_OVERRIDE}")
file(READ "${TOS_SPI_SOURCE}/src/gpspi/spi_master.c" TOS_SPI_CONTENT)
string(REPLACE
    "if (trans_buf->buffer_to_rcv != orig_rx_buffer) {"
    "if (trans_buf->buffer_to_rcv != NULL && trans_buf->buffer_to_rcv != orig_rx_buffer) {"
    TOS_SPI_PATCHED "${TOS_SPI_CONTENT}")
file(READ "${TOS_SPI_OVERRIDE}/src/gpspi/spi_master.c" TOS_SPI_PREVIOUS)
if(NOT TOS_SPI_PREVIOUS STREQUAL TOS_SPI_PATCHED)
    file(WRITE "${TOS_SPI_OVERRIDE}/src/gpspi/spi_master.c" "${TOS_SPI_PATCHED}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${TOS_SPI_SOURCE}/src/gpspi/spi_master.c")
list(APPEND EXTRA_COMPONENT_DIRS "${TOS_SPI_OVERRIDE}")
