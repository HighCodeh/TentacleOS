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

#include "console_service.h"

#include <stdio.h>

#include "esp_console.h"
#include "esp_err.h"

#include "resource_mgr.h"

#define RES_SNAPSHOT_MAX 16

static int cmd_resources(int argc, char **argv) {
  (void)argc;
  (void)argv;
  res_status_t rows[RES_SNAPSHOT_MAX];
  int n = resource_snapshot(rows, RES_SNAPSHOT_MAX);
  printf("%-10s %-6s %5s %5s %5s  holder\n", "resource", "lane", "cap", "resv", "used");
  for (int i = 0; i < n; i++) {
    res_status_t *r = &rows[i];
    printf("%-10s %-6s %5u %5u %5u  %s\n", r->res_name,
           (r->lane_name && r->lane_name[0]) ? r->lane_name : "-", r->capacity, r->reserved,
           r->used, r->used ? resource_owner_name(r->holder) : "-");
  }
  return 0;
}

void register_resource_commands(void) {
  const esp_console_cmd_t resources = {.command = "resources",
                                       .help = "Show radio/peripheral arbitration state",
                                       .hint = NULL,
                                       .func = cmd_resources};
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&resources));
}
