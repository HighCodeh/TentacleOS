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
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"

#include "led_control.h"

static int cmd_led(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: led <color|clear|blink|signal>\n");
    printf("  color <r> <g> <b>           set the LED color (0-255 each)\n");
    printf("  clear                        turn the LED off\n");
    printf("  blink <r> <g> <b> <ms>       blink once for <ms> milliseconds\n");
    printf("  signal <info|warn|error>     play a semantic status cue\n");
    return 1;
  }

  if (strcmp(argv[1], "color") == 0) {
    if (argc < 5) {
      printf("usage: led color <r> <g> <b>\n");
      return 1;
    }
    led_set_color((uint8_t)strtoul(argv[2], NULL, 0),
                  (uint8_t)strtoul(argv[3], NULL, 0),
                  (uint8_t)strtoul(argv[4], NULL, 0));
    return 0;
  }

  if (strcmp(argv[1], "clear") == 0) {
    led_clear();
    return 0;
  }

  if (strcmp(argv[1], "blink") == 0) {
    if (argc < 6) {
      printf("usage: led blink <r> <g> <b> <ms>\n");
      return 1;
    }
    led_blink((uint8_t)strtoul(argv[2], NULL, 0),
              (uint8_t)strtoul(argv[3], NULL, 0),
              (uint8_t)strtoul(argv[4], NULL, 0),
              (int)strtoul(argv[5], NULL, 0));
    return 0;
  }

  if (strcmp(argv[1], "signal") == 0) {
    if (argc < 3) {
      printf("usage: led signal <info|warn|error>\n");
      return 1;
    }
    if (strcmp(argv[2], "info") == 0)
      led_signal_info();
    else if (strcmp(argv[2], "warn") == 0 || strcmp(argv[2], "warning") == 0)
      led_signal_warning();
    else if (strcmp(argv[2], "error") == 0)
      led_signal_error();
    else {
      printf("led: unknown signal '%s'\n", argv[2]);
      return 1;
    }
    return 0;
  }

  printf("led: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

void register_led_commands(void) {
  const esp_console_cmd_t cmd_led_def = {
      .command = "led",
      .help = "RGB status LED: led color | clear | blink | signal",
      .hint = NULL,
      .func = &cmd_led,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_led_def));
}
