// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef HOST_LINK_FILES_H
#define HOST_LINK_FILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// P4-local file operations for the companion host link (FILE_* ops). The P4
// owns both filesystems (internal flash + micro-SD); these handlers are run
// locally, never relayed to the C5. Paths are sandboxed to the mounted roots.
//
// Largest data carried in one FILE_WRITE data segment and generic local resp.
#define HOST_FILE_DATA_MAX 1024

// Largest FILE_READ chunk. Bigger => fewer round-trips reading large assets.
// Must leave room in HOST_LINK_MAX_FRAME (header + body header + status + MAC).
#define HOST_FILE_CHUNK 3072

/**
 * @brief Execute a file op locally.
 *
 * @param cmd      Packed command id (SPI_ID_FILE_*).
 * @param payload  Request payload (layout per op; see host_link_files.c).
 * @param plen     Payload length.
 * @param out_data Response data buffer (written without the status byte).
 * @param out_cap  Capacity of @p out_data.
 * @param out_len  Receives the response data length.
 * @return spi_status_t value (OK / ERROR / INVALID_ARG / ...).
 */
uint8_t host_files_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out_data,
                          uint16_t out_cap,
                          uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_FILES_H
