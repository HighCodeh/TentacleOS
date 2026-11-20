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

#ifndef HOST_PC_MONITOR_H
#define HOST_PC_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_PC_MONITOR_VERSION     1
#define HOST_PC_MONITOR_WIRE_SIZE   180
#define HOST_PC_MONITOR_NAME_SIZE   32
#define HOST_PC_MONITOR_IP_SIZE     46
#define HOST_PC_MONITOR_PERCENT_MAX 10000
#define HOST_PC_MONITOR_STALE_US    5000000

typedef struct {
  uint16_t cpu;
  uint16_t ram;
  uint16_t disk;
  uint32_t ram_used_mib;
  uint32_t ram_total_mib;
  uint32_t disk_used_mib;
  uint32_t disk_total_mib;
  char hostname[HOST_PC_MONITOR_NAME_SIZE];
  char volume[HOST_PC_MONITOR_NAME_SIZE];
  char local_ip[HOST_PC_MONITOR_IP_SIZE];
  char public_ip[HOST_PC_MONITOR_IP_SIZE];
} host_pc_monitor_snapshot_t;

/* Returns a caller-owned copy only while the authenticated sample is fresh. */
bool host_pc_monitor_get(host_pc_monitor_snapshot_t *out_snapshot);
uint8_t host_pc_monitor_handle(uint16_t cmd,
                               const uint8_t *payload,
                               uint16_t plen,
                               uint8_t *out,
                               uint16_t out_cap,
                               uint16_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
