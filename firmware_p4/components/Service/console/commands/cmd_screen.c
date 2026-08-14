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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "lvgl_glue.h"
#include "st7789.h"
#include "ui_manager.h"

#define SHOT_SETTLE_MS   600
#define SHOT_MAX_WAIT_MS 30000
#define SHOT_POLL_MS     20
#define SHOT_B64_BUF     1400
#define KEY_DEFAULT_MS   120
#define SHOT_MAX_ROWS    320

static volatile uint8_t s_cov[SHOT_MAX_ROWS];

static struct {
  struct arg_int *id;
  struct arg_end *end;
} goto_args;

static struct {
  struct arg_int *idx;
  struct arg_int *ms;
  struct arg_end *end;
} key_args;

static struct {
  struct arg_int *id;
  struct arg_end *end;
} shot_args;

static int cmd_goto(int argc, char **argv) {
  int nerr = arg_parse(argc, argv, (void **)&goto_args);
  if (nerr != 0) {
    arg_print_errors(stderr, goto_args.end, "goto");
    return 1;
  }
  int id = goto_args.id->ival[0];
  if (id <= SCREEN_NONE || id >= SCREEN_COUNT) {
    printf("goto: id out of range (1..%d)\n", SCREEN_COUNT - 1);
    return 1;
  }
  ui_switch_screen((screen_id_t)id);
  printf("goto %d\n", id);
  return 0;
}

static int cmd_key(int argc, char **argv) {
  int nerr = arg_parse(argc, argv, (void **)&key_args);
  if (nerr != 0) {
    arg_print_errors(stderr, key_args.end, "key");
    return 1;
  }
  int idx = key_args.idx->ival[0];
  int ms = key_args.ms->count ? key_args.ms->ival[0] : KEY_DEFAULT_MS;
  buttons_sim_press(idx, (uint32_t)ms);
  printf("key %d %d\n", idx, ms);
  return 0;
}

static void strip_sink(
    int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t *rgb565, int32_t stride_bytes) {
  int w = (int)(x2 - x1 + 1);
  int h = (int)(y2 - y1 + 1);
  if (w <= 0 || h <= 0) {
    return;
  }
  printf("[[STRIP]] %d %d %d %d\n", (int)x1, (int)y1, (int)x2, (int)y2);
  static char b64[SHOT_B64_BUF];
  for (int row = 0; row < h; row++) {
    const uint8_t *line = rgb565 + (size_t)row * stride_bytes;
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)b64, sizeof(b64), &olen, line, (size_t)w * 2) == 0) {
      b64[olen] = '\0';
      printf("%s\n", b64);
    }
    int y = (int)y1 + row;
    if (y >= 0 && y < SHOT_MAX_ROWS) {
      s_cov[y] = 1;
    }
  }
}

static int cmd_screenshot(int argc, char **argv) {
  int nerr = arg_parse(argc, argv, (void **)&shot_args);
  if (nerr != 0) {
    arg_print_errors(stderr, shot_args.end, "screenshot");
    return 1;
  }

  if (shot_args.id->count) {
    int id = shot_args.id->ival[0];
    if (id > SCREEN_NONE && id < SCREEN_COUNT) {
      ui_switch_screen((screen_id_t)id);
      vTaskDelay(pdMS_TO_TICKS(SHOT_SETTLE_MS));
    }
  }

  lv_display_t *disp = lv_display_get_default();
  int hres = (int)lv_display_get_horizontal_resolution(disp);
  int vres = (int)lv_display_get_vertical_resolution(disp);
  if (vres > SHOT_MAX_ROWS) {
    vres = SHOT_MAX_ROWS;
  }

  printf("[[SHOT]] W=%d H=%d FMT=RGB565\n", hres, vres);

  memset((void *)s_cov, 0, sizeof(s_cov));
  lvgl_glue_capture_begin(strip_sink);

  if (ui_acquire()) {
    lv_obj_invalidate(lv_screen_active());
    ui_release();
  }

  int waited = 0;
  while (waited < SHOT_MAX_WAIT_MS) {
    bool all = true;
    for (int y = 0; y < vres; y++) {
      if (!s_cov[y]) {
        all = false;
        break;
      }
    }
    if (all) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(SHOT_POLL_MS));
    waited += SHOT_POLL_MS;
  }

  lvgl_glue_capture_end();
  printf("[[/SHOT]]\n");
  return 0;
}

void register_screen_commands(void) {
  goto_args.id = arg_int1(NULL, NULL, "<id>", "screen_id_t value (1..SCREEN_COUNT-1)");
  goto_args.end = arg_end(1);
  const esp_console_cmd_t goto_cmd = {
      .command = "goto",
      .help = "Switch the UI to a screen by enum id: goto <id>",
      .func = &cmd_goto,
      .argtable = &goto_args,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&goto_cmd));

  key_args.idx = arg_int1(NULL, NULL, "<idx>", "0=UP 1=DOWN 2=LEFT 3=RIGHT 4=OK 5=BACK");
  key_args.ms = arg_int0("t", "ms", "<ms>", "hold duration in ms (default 120)");
  key_args.end = arg_end(2);
  const esp_console_cmd_t key_cmd = {
      .command = "key",
      .help = "Inject a simulated button press: key <idx> [-t <ms>]",
      .func = &cmd_key,
      .argtable = &key_args,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&key_cmd));

  shot_args.id = arg_int0(NULL, NULL, "<id>", "optional: switch to <id> before capturing");
  shot_args.end = arg_end(1);
  const esp_console_cmd_t shot_cmd = {
      .command = "screenshot",
      .help = "Capture the active screen as base64 RGB565 strips: screenshot [id]",
      .func = &cmd_screenshot,
      .argtable = &shot_args,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&shot_cmd));
}
