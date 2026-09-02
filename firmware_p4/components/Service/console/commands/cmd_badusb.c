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

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"

#include "bad_ble.h"
#include "bad_usb.h"
#include "ducky_parser.h"
#include "tinyusb.h"

#define BADUSB_TYPE_BUF_SIZE  256
#define BADUSB_ASSET_DIR      "storage/bad_usb_scripts/"
#define BADUSB_ASSET_PATH_MAX 128

// Bring the HID side of the composite up and register the keyboard/mouse
// callbacks. busb_init() is idempotent, so this is safe even though the host
// link already installed TinyUSB at boot.
static esp_err_t ensure_badusb_ready(void) {
  esp_err_t err = bad_usb_init();
  if (err == ESP_ERR_INVALID_STATE) {
    return ESP_OK; // already initialized
  }
  return err;
}

// --- RUN ---
static struct {
  struct arg_str *asset;
  struct arg_str *file;
  struct arg_lit *ble;
  struct arg_end *end;
} s_run_args;

static bool start_ble_target(void) {
  if (bad_ble_init() != ESP_OK) {
    printf("Failed to start BadBLE.\n");
    return false;
  }
  ducky_set_output_mode(DUCKY_OUTPUT_BLUETOOTH);
  printf("Waiting for a Bluetooth host to pair...\n");
  if (!bad_ble_wait_for_connection_ex(NULL)) {
    printf("No Bluetooth host connected.\n");
    bad_ble_deinit();
    return false;
  }
  return true;
}

static int subcmd_run(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&s_run_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, s_run_args.end, "badusb run");
    return 1;
  }

  if (s_run_args.asset->count == 0 && s_run_args.file->count == 0) {
    printf("Usage: badusb run -a <asset> | -f <sd_path> [-b]\n");
    return 1;
  }

  bool use_ble = (s_run_args.ble->count > 0);
  if (use_ble) {
    if (!start_ble_target())
      return 1;
  } else {
    if (ensure_badusb_ready() != ESP_OK) {
      printf("Failed to initialize BadUSB.\n");
      return 1;
    }
    ducky_set_output_mode(DUCKY_OUTPUT_USB);
  }

  esp_err_t err;
  if (s_run_args.asset->count > 0) {
    char asset_path[BADUSB_ASSET_PATH_MAX];
    snprintf(asset_path, sizeof(asset_path), "%s%s", BADUSB_ASSET_DIR, s_run_args.asset->sval[0]);
    printf("Running asset script '%s'...\n", asset_path);
    err = ducky_run_from_assets(asset_path);
  } else {
    const char *path = s_run_args.file->sval[0];
    printf("Running SD script '%s'...\n", path);
    err = ducky_run_from_sdcard(path);
  }

  if (use_ble)
    bad_ble_deinit();

  if (err == ESP_OK) {
    printf("Script finished.\n");
    return 0;
  }
  printf("Script failed: %s\n", esp_err_to_name(err));
  return 1;
}

// --- TYPE ---
static int subcmd_type(int sub_argc, char **sub_argv) {
  if (sub_argc < 2) {
    printf("Usage: badusb type <text...>\n");
    return 1;
  }

  if (ensure_badusb_ready() != ESP_OK) {
    printf("Failed to initialize BadUSB.\n");
    return 1;
  }
  ducky_set_output_mode(DUCKY_OUTPUT_USB);

  // Re-join the remaining argv tokens into a single DuckyScript STRING line.
  char script[BADUSB_TYPE_BUF_SIZE];
  int n = snprintf(script, sizeof(script), "STRING ");
  for (int i = 1; i < sub_argc && n < (int)sizeof(script); i++) {
    n += snprintf(script + n, sizeof(script) - n, "%s%s", (i > 1) ? " " : "", sub_argv[i]);
  }

  printf("Typing: %s\n", script + strlen("STRING "));
  ducky_parse_and_run(script);
  printf("Done.\n");
  return 0;
}

// --- LAYOUT ---
static int subcmd_layout(int sub_argc, char **sub_argv) {
  if (sub_argc < 2) {
    printf("Usage: badusb layout <us|abnt2>\n");
    return 1;
  }

  const char *l = sub_argv[1];
  if (strcasecmp(l, "us") == 0) {
    ducky_set_layout(DUCKY_LAYOUT_US);
    printf("Layout set to US.\n");
  } else if (strcasecmp(l, "abnt2") == 0) {
    ducky_set_layout(DUCKY_LAYOUT_ABNT2);
    printf("Layout set to ABNT2.\n");
  } else {
    printf("Unknown layout '%s'. Use: us | abnt2\n", l);
    return 1;
  }
  return 0;
}

// --- STOP ---
static int subcmd_stop(void) {
  ducky_abort();
  printf("Abort requested (stops at next line).\n");
  return 0;
}

// --- STATUS ---
static int subcmd_status(void) {
  printf("--- BadUSB Status ---\n");
  printf("USB mounted: %s\n", tud_mounted() ? "Yes" : "No");
  return 0;
}

static int cmd_badusb(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: badusb <command> [options]\n\n");
    printf("Commands:\n");
    printf("  run     Run a DuckyScript\n");
    printf("          -a <asset> (internal) | -f <sd_path>  [-b for Bluetooth]\n");
    printf("  type    Type a literal string over HID\n");
    printf("          type <text...>\n");
    printf("  layout  Set keyboard layout\n");
    printf("          layout <us|abnt2>\n");
    printf("  stop    Abort the running script\n");
    printf("  status  Show USB mount status\n");
    return 0;
  }

  const char *subcmd = argv[1];
  int sub_argc = argc - 1;
  char **sub_argv = &argv[1];

  if (strcmp(subcmd, "run") == 0)
    return subcmd_run(sub_argc, sub_argv);
  if (strcmp(subcmd, "type") == 0)
    return subcmd_type(sub_argc, sub_argv);
  if (strcmp(subcmd, "layout") == 0)
    return subcmd_layout(sub_argc, sub_argv);
  if (strcmp(subcmd, "stop") == 0)
    return subcmd_stop();
  if (strcmp(subcmd, "status") == 0)
    return subcmd_status();

  printf("Unknown badusb command: %s\n", subcmd);
  return 1;
}

void register_badusb_commands(void) {
  s_run_args.asset = arg_str0("a", "asset", "<name>", "Run script from internal bad_usb_scripts");
  s_run_args.file = arg_str0("f", "file", "<path>", "Run script from SD card");
  s_run_args.ble = arg_lit0("b", "ble", "Send over Bluetooth HID instead of USB");
  s_run_args.end = arg_end(1);

  const esp_console_cmd_t badusb_cmd = {.command = "badusb",
                                        .help = "BadUSB HID injection (DuckyScript)",
                                        .hint = "<run|type|layout|stop|status> ...",
                                        .func = &cmd_badusb,
                                        .argtable = NULL};
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&badusb_cmd));
}
