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

#ifndef PORT_SCANNER_STORE_H
#define PORT_SCANNER_STORE_H

#include "port_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_SCANNER_STORE_DIR "/sdcard/wifi/port_scans"

typedef bool (*port_scanner_store_cancel_cb_t)(void);

esp_err_t port_scanner_store_create(char *out_path, size_t capacity, uint32_t first, uint32_t last);
esp_err_t
port_scanner_store_append(const char *path, const port_scan_result_t *results, uint16_t count);
esp_err_t port_scanner_store_read(const char *path,
                                  port_scanner_page_t *out_page,
                                  port_scanner_store_cancel_cb_t is_cancelled);
esp_err_t port_scanner_store_list(port_scanner_page_t *out_page,
                                  port_scanner_store_cancel_cb_t is_cancelled);
esp_err_t port_scanner_store_browse(const port_scanner_browse_request_t *request,
                                    port_scanner_page_t *out_page,
                                    port_scanner_store_cancel_cb_t is_cancelled);
esp_err_t port_scanner_store_delete(const char *path);

#ifdef __cplusplus
}
#endif
#endif
