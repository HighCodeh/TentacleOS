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
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"

#include "host_link_ble.h"
#include "host_link_sec.h"

static int subcmd_psk(void) {
  char hex[HOST_LINK_PSK_HEX_SIZE];
  esp_err_t err = host_link_sec_get_psk_hex(hex, sizeof(hex));
  if (err != ESP_OK) {
    printf("PSK unavailable: %s\n", esp_err_to_name(err));
    return 1;
  }
  printf("Pairing PSK (provision to the companion app):\n  %s\n", hex);
  return 0;
}

static int subcmd_regen(void) {
  esp_err_t err = host_link_sec_regenerate_psk();
  if (err != ESP_OK) {
    printf("Regenerate failed: %s\n", esp_err_to_name(err));
    return 1;
  }
  printf("New PSK generated. Existing pairings are now invalid.\n");
  return subcmd_psk();
}

static int subcmd_status(void) {
  printf("--- Host Link Status ---\n");
  printf("Session: %s\n", host_link_sec_is_authenticated() ? "AUTHENTICATED" : "not paired");
  printf("BLE:     %s\n", host_link_ble_is_connected() ? "connected" : "no companion");
  return 0;
}

static int subcmd_ble(int sub_argc, char **sub_argv) {
  if (sub_argc < 2) {
    printf("Usage: hostlink ble <on|off>\n");
    return 1;
  }

  const char *arg = sub_argv[1];
  esp_err_t err;
  if (strcmp(arg, "on") == 0) {
    err = host_link_ble_start();
  } else if (strcmp(arg, "off") == 0) {
    err = host_link_ble_stop();
  } else {
    printf("Usage: hostlink ble <on|off>\n");
    return 1;
  }

  if (err != ESP_OK) {
    printf("BLE %s failed: %s\n", arg, esp_err_to_name(err));
    return 1;
  }
  printf("BLE companion %s.\n", (strcmp(arg, "on") == 0) ? "advertising" : "stopped");
  return 0;
}

static int cmd_hostlink(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: hostlink <command>\n\n");
    printf("Commands:\n");
    printf("  psk      Show the pairing PSK (for QR/manual provisioning)\n");
    printf("  regen    Generate a new PSK (invalidates current pairings)\n");
    printf("  ble      Companion BLE on/off (ble <on|off>)\n");
    printf("  status   Show the companion session state\n");
    return 0;
  }

  const char *subcmd = argv[1];
  if (strcmp(subcmd, "psk") == 0)
    return subcmd_psk();
  if (strcmp(subcmd, "regen") == 0)
    return subcmd_regen();
  if (strcmp(subcmd, "ble") == 0)
    return subcmd_ble(argc - 1, &argv[1]);
  if (strcmp(subcmd, "status") == 0)
    return subcmd_status();

  printf("Unknown hostlink command: %s\n", subcmd);
  return 1;
}

void register_hostlink_commands(void) {
  const esp_console_cmd_t hostlink_cmd = {.command = "hostlink",
                                          .help = "Companion host-link pairing & status",
                                          .hint = "<psk|regen|status>",
                                          .func = &cmd_hostlink,
                                          .argtable = NULL};
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&hostlink_cmd));
}
