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
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "wardrive.h"
#include "ui_manager.h"
static const char *TAG = "WARDRIVE_CLI";
static int command(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "open")) {
    ui_switch_screen(SCREEN_WARDRIVE);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "start"))
    return wardrive_start(0) == ESP_OK ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "stop")) {
    wardrive_stop();
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "pause")) {
    wardrive_pause(true);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "resume")) {
    wardrive_pause(false);
    return 0;
  }
  if (argc > 2 || (argc == 2 && strcmp(argv[1], "status")))
    return 1;
  wardrive_status_t status;
  wardrive_get_status(&status);
  ESP_LOGI(TAG,
           "running=%u cycles=%lu saved=%lu buffered=%lu GPS=%u UART=%lu error=%s path=%s",
           status.running,
           (unsigned long)status.cycles,
           (unsigned long)status.saved,
           (unsigned long)status.buffered,
           status.has_fix,
           (unsigned long)status.gps_bytes,
           esp_err_to_name(status.radio_error),
           status.path);
  return 0;
}
void register_wardrive_commands(void) {
  const esp_console_cmd_t cmd = {.command = "wardrive",
                                 .help = "open | start | stop | pause | resume | status",
                                 .func = command};
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd));
}
