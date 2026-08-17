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

#include "driver/i2c_master.h"
#include "esp_console.h"

#include "i2c_init.h"

#define I2C_SCAN_FIRST_ADDR 0x08
#define I2C_SCAN_LAST_ADDR  0x77
#define I2C_SCAN_PROBE_MS   20

static int cmd_i2cscan(int argc, char **argv) {
  (void)argc;
  (void)argv;

  i2c_master_bus_handle_t bus = i2c_get_bus();
  if (bus == NULL) {
    printf("I2C bus not initialized\n");
    return 1;
  }

  printf("Scanning I2C (0x%02X-0x%02X)...\n", I2C_SCAN_FIRST_ADDR, I2C_SCAN_LAST_ADDR);
  int found = 0;
  for (uint8_t addr = I2C_SCAN_FIRST_ADDR; addr <= I2C_SCAN_LAST_ADDR; addr++) {
    if (i2c_master_probe(bus, addr, I2C_SCAN_PROBE_MS) == ESP_OK) {
      printf("  0x%02X\n", addr);
      found++;
    }
  }
  printf("%d device(s) found\n", found);
  return 0;
}

void register_i2c_commands(void) {
  const esp_console_cmd_t cmd_i2cscan_def = {
      .command = "i2cscan",
      .help = "Scan the I2C bus and list responding addresses",
      .hint = NULL,
      .func = &cmd_i2cscan,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_i2cscan_def));
}
