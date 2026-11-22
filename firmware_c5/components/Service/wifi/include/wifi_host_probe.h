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

#ifndef WIFI_HOST_PROBE_H
#define WIFI_HOST_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_HOST_PROBE_MAX_HOSTS 8

// Consecutive IPv4 addresses; first_ip is network byte order. Bit i is host i.
esp_err_t wifi_host_probe_run_batch(uint32_t first_ip, uint8_t count, uint16_t *out_reachable);

// Run on the radio worker; ESP_OK with false means no matching echo reply.
esp_err_t wifi_host_probe_run(const char *ip, bool *out_reachable);
void wifi_host_probe_cancel(void);
void wifi_host_probe_reset(void);

#ifdef __cplusplus
}
#endif
#endif
