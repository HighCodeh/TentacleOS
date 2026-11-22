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

#ifndef PORT_SCANNER_H
#define PORT_SCANNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "port_scan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_SCANNER_IP_LENGTH     16
#define PORT_SCANNER_TARGET_LENGTH 48
#define PORT_SCANNER_PORTS_LENGTH  192
#define PORT_SCANNER_RESULTS_MAX   128
#define PORT_SCANNER_PAGE_SIZE     8
#define PORT_SCANNER_PATH_LENGTH   96
#define PORT_SCANNER_NAME_LENGTH   48

typedef enum {
  PORT_SCANNER_GATEWAY,
  PORT_SCANNER_HOST,
  PORT_SCANNER_LOCAL,
  PORT_SCANNER_RANGE,
  PORT_SCANNER_CIDR,
  PORT_SCANNER_TARGET_COUNT
} port_scanner_target_t;

typedef enum {
  PORT_SCANNER_COMMON_PORTS,
  PORT_SCANNER_CUSTOM_PORTS,
  PORT_SCANNER_ALL_PORTS,
  PORT_SCANNER_PORTS_COUNT
} port_scanner_ports_t;

typedef enum {
  PORT_SCANNER_IDLE,
  PORT_SCANNER_RUNNING,
  PORT_SCANNER_STOPPING,
  PORT_SCANNER_DONE,
  PORT_SCANNER_CANCELLED,
  PORT_SCANNER_FAILED,
  PORT_SCANNER_STATE_COUNT
} port_scanner_state_t;

typedef enum {
  PORT_SCANNER_TCP,
  PORT_SCANNER_UDP,
  PORT_SCANNER_BOTH,
  PORT_SCANNER_PROTOCOL_COUNT
} port_scanner_protocol_t;

typedef struct {
  port_scanner_protocol_t protocol_mode;
  port_scanner_target_t target_mode;
  port_scanner_ports_t port_mode;
  bool is_ping_enabled;
  char target[PORT_SCANNER_TARGET_LENGTH];
  char ports[PORT_SCANNER_PORTS_LENGTH];
} port_scanner_config_t;

typedef struct {
  port_scanner_state_t state;
  esp_err_t error;
  bool is_busy;
  bool is_probing;
  bool is_ping_enabled;
  bool is_discovery_complete;
  uint8_t probe_count;
  uint32_t hosts_probed;
  uint32_t hosts_done;
  uint32_t hosts_total;
  uint32_t hosts_skipped;
  uint32_t hosts_responded;
  uint32_t ports_done;
  uint32_t ports_total;
  uint32_t found;
  uint32_t uncertain;
  uint32_t filtered;
  port_scan_protocol_t active_protocol;
  uint16_t stored;
  uint32_t saved;
  esp_err_t storage_error;
  char archive_path[PORT_SCANNER_PATH_LENGTH];
  char current_ip[PORT_SCANNER_IP_LENGTH];
} port_scanner_snapshot_t;

typedef struct {
  char ip[PORT_SCANNER_IP_LENGTH];
  uint32_t first_record;
  uint32_t count;
} port_scanner_host_t;

typedef enum {
  PORT_SCANNER_BROWSE_NETWORKS,
  PORT_SCANNER_BROWSE_HOSTS,
  PORT_SCANNER_BROWSE_PORTS,
  PORT_SCANNER_BROWSE_RECORDS,
  PORT_SCANNER_BROWSE_DELETE,
  PORT_SCANNER_BROWSE_COUNT
} port_scanner_browse_kind_t;

typedef struct {
  port_scanner_browse_kind_t kind;
  char path[PORT_SCANNER_PATH_LENGTH];
  uint32_t number;
  uint32_t cursor;
  bool is_previous;
  port_scanner_host_t host;
} port_scanner_browse_request_t;

typedef struct {
  bool is_busy;
  bool is_partial;
  bool has_next;
  esp_err_t error;
  uint32_t number;
  uint32_t total;
  uint32_t cursor;
  uint32_t next_cursor;
  uint16_t count;
  port_scan_result_t results[PORT_SCANNER_PAGE_SIZE];
  char files[PORT_SCANNER_PAGE_SIZE][PORT_SCANNER_NAME_LENGTH];
  char labels[PORT_SCANNER_PAGE_SIZE][PORT_SCANNER_TARGET_LENGTH];
  port_scanner_host_t hosts[PORT_SCANNER_PAGE_SIZE];
} port_scanner_page_t;

esp_err_t port_scanner_validate(const port_scanner_config_t *config);
esp_err_t port_scanner_start(const port_scanner_config_t *config);
void port_scanner_stop(void);
void port_scanner_get_snapshot(port_scanner_snapshot_t *out_snapshot);
bool port_scanner_get_result(uint16_t index, port_scan_result_t *out_result);
// NULL path lists archives; file paths read one page. Rejected during scans.
esp_err_t port_scanner_request_page(const char *path, uint32_t page);
esp_err_t port_scanner_browse(const port_scanner_browse_request_t *request);
void port_scanner_get_page(port_scanner_page_t *out_page);
void port_scanner_close_page(void);

#ifdef __cplusplus
}
#endif
#endif
