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

#include "vfs_core.h"
#include "vfs_sdcard.h"

#define SD_PATH      "/sdcard"
#define BYTES_PER_GB 1000000000ULL

static struct {
  struct arg_str *action;
  struct arg_lit *yes;
  struct arg_end *end;
} s_sd_args;

static void print_status(void) {
  if (!vfs_sdcard_is_mounted()) {
    printf("SD: not mounted\n");
    return;
  }

  char name[24] = "SD Card";
  vfs_sdcard_get_name(name, sizeof(name));

  printf("SD: mounted\n");
  printf("  name  : %s\n", name);
  printf("  format: %s\n", vfs_sdcard_fs_type());

  vfs_statvfs_t st = {0};
  if (vfs_statvfs(SD_PATH, &st) == ESP_OK && st.total_bytes > 0) {
    unsigned total = (unsigned)((st.total_bytes + BYTES_PER_GB / 2) / BYTES_PER_GB);
    unsigned used_t = (unsigned)((st.used_bytes * 10ULL) / BYTES_PER_GB);
    unsigned free_t = (unsigned)((st.free_bytes * 10ULL) / BYTES_PER_GB);
    printf("  size  : %u GB\n", total);
    printf("  used  : %u.%u GB\n", used_t / 10, used_t % 10);
    printf("  free  : %u.%u GB\n", free_t / 10, free_t % 10);
  }
}

static int do_toexfat(void) {
  if (!vfs_sdcard_is_mounted()) {
    printf("error: no SD card mounted\n");
    return 1;
  }

  const char *type = vfs_sdcard_fs_type();
  if (strcmp(type, "exFAT") == 0) {
    printf("SD is already exFAT, nothing to do\n");
    return 0;
  }

  if (s_sd_args.yes->count == 0) {
    printf("SD is %s. Reformatting to exFAT ERASES ALL DATA.\n", type);
    printf("Re-run to confirm:  sd toexfat --yes\n");
    return 1;
  }

  printf("Formatting %s -> exFAT (erasing all data)...\n", type);
  esp_err_t r = vfs_sdcard_format_exfat();
  if (r != ESP_OK) {
    printf("error: format failed (%s)\n", esp_err_to_name(r));
    return 1;
  }
  printf("done. SD is now %s\n", vfs_sdcard_fs_type());
  return 0;
}

static int cmd_sd(int argc, char **argv) {
  int nerr = arg_parse(argc, argv, (void **)&s_sd_args);
  if (nerr != 0) {
    arg_print_errors(stderr, s_sd_args.end, argv[0]);
    return 1;
  }

  const char *action = s_sd_args.action->count ? s_sd_args.action->sval[0] : "status";

  if (strcmp(action, "status") == 0) {
    print_status();
    return 0;
  }
  if (strcmp(action, "toexfat") == 0) {
    return do_toexfat();
  }

  printf("unknown action '%s' (use: status | toexfat)\n", action);
  return 1;
}

void register_sd_commands(void) {
  s_sd_args.action =
      arg_str0(NULL, NULL, "<status|toexfat>", "action (default: status)");
  s_sd_args.yes = arg_lit0("y", "yes", "confirm the destructive reformat");
  s_sd_args.end = arg_end(2);

  const esp_console_cmd_t cmd_sd_def = {
      .command = "sd",
      .help = "SD card: 'sd' shows the current format, 'sd toexfat --yes' "
              "reformats FAT32 to exFAT (erases all data)",
      .hint = NULL,
      .func = &cmd_sd,
      .argtable = &s_sd_args,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_sd_def));
}
