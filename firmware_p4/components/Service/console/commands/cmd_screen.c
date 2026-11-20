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
#include <stdint.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "driver/uart.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "lvgl_glue.h"
#include "st7789.h"
#include "ui_manager.h"

#define SHOT_SETTLE_MS       600
#define SHOT_MAX_WAIT_MS     30000
#define SHOT_POLL_MS         20
#define SHOT_B64_BUF         1400
#define KEY_DEFAULT_MS       120
#define SHOT_MAX_ROWS        320
#define SHOT_RLE_RUN_MAX     255
#define SHOT_RLE_BYTES       3
#define SHOT_FAST_GUARD_MS   40
#define SHOT_DRAIN_MS        1000
#define SHOT_MAX_BAUD        2000000
#define SHOT_STANDARD_BAUD   921600
#define SHOT_SLOW_BAUD       460800
#define SHOT_ROW_YIELD_TICKS 1

static const char *TAG = "SCREEN_CONSOLE";
static uint8_t s_cov[SHOT_MAX_ROWS];
static uint8_t *s_frame;
static int s_width;
static int s_height;
static uint8_t *s_previous;
static int s_previous_width, s_previous_height;
static uint32_t s_sequence;
static bool s_capture_done; /* Protected by the LVGL lock. */

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
  struct arg_lit *rle;
  struct arg_int *baud;
  struct arg_int *delta;
  struct arg_end *end;
} shot_args;

typedef struct {
  char b64[SHOT_B64_BUF];
  uint8_t packed[SHOT_MAX_ROWS * SHOT_RLE_BYTES];
} shot_buffers_t;

static void switch_screen_async(void *context) {
  ui_switch_screen((screen_id_t)(intptr_t)context);
}

static bool queue_screen_switch(screen_id_t screen) {
  if (!ui_acquire())
    return false;
  bool queued = lv_async_call(switch_screen_async, (void *)(intptr_t)screen) == LV_RESULT_OK;
  ui_release();
  if (!queued)
    ESP_LOGE(TAG, "Unable to queue screen switch");
  return queued;
}

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
  // Recent-view snapshots may render here; use the graphics task's stack.
  if (!queue_screen_switch((screen_id_t)id))
    return 1;
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
  if (s_frame == NULL || rgb565 == NULL || w <= 0 || h <= 0 || x1 < 0 || y1 < 0 || x2 >= s_width ||
      y2 >= s_height || stride_bytes < w * 2)
    return;
  /* Called with the LVGL mutex and SPI lock held: never print here. */
  for (int row = 0; row < h; row++) {
    int y = y1 + row;
    memcpy(s_frame + ((size_t)y * s_width + x1) * 2,
           rgb565 + (size_t)row * stride_bytes,
           (size_t)w * 2);
    if (x1 == 0 && w == s_width)
      s_cov[y] = 1;
  }
}

static void capture_refresh(void *context) {
  (void)context;
  if (s_frame == NULL)
    return;
  /* Rendering uses the 16 KiB graphics-task stack, never the 8 KiB console stack. */
  lvgl_glue_capture_begin(strip_sink);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(lv_display_get_default());
  lvgl_glue_capture_end();
  s_capture_done = true;
}

static size_t encode_rle_row(uint8_t *out, const uint8_t *pixels, size_t width) {
  size_t written = 0;
  for (size_t x = 0; x < width;) {
    size_t count = 1;
    while (count < SHOT_RLE_RUN_MAX && x + count < width &&
           pixels[(x + count) * 2] == pixels[x * 2] &&
           pixels[(x + count) * 2 + 1] == pixels[x * 2 + 1])
      count++;
    out[written++] = (uint8_t)count;
    out[written++] = pixels[x * 2];
    out[written++] = pixels[x * 2 + 1];
    x += count;
  }
  return written;
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
      if (!queue_screen_switch((screen_id_t)id))
        return 1;
      vTaskDelay(pdMS_TO_TICKS(SHOT_SETTLE_MS));
    }
  }

  int result = 1;
  bool is_fast = false;
  int baud = shot_args.baud->count ? shot_args.baud->ival[0] : 0;
  if (baud && baud != SHOT_SLOW_BAUD && baud != SHOT_STANDARD_BAUD && baud != SHOT_MAX_BAUD)
    return 1;
  const bool is_delta = shot_args.delta->count != 0;
  if (is_delta && shot_args.delta->ival[0] < 0)
    return 1;
  uint8_t *frame = NULL;
  shot_buffers_t *buffers = NULL;
  int width = 0, height = 0;
  if (!ui_acquire())
    return 1;
  /* Cancel a retained request before releasing its buffer after a timeout. */
  lv_async_call_cancel(capture_refresh, NULL);
  lvgl_glue_capture_end();
  heap_caps_free(s_frame);
  s_frame = NULL;
  lv_display_t *disp = lv_display_get_default();
  if (disp != NULL) {
    width = (int)lv_display_get_horizontal_resolution(disp);
    height = (int)lv_display_get_vertical_resolution(disp);
  }
  if (width <= 0 || width > SHOT_MAX_ROWS || height <= 0 || height > SHOT_MAX_ROWS) {
    ui_release();
    ESP_LOGE(TAG, "Unsupported capture dimensions");
    return 1;
  }
  if (!is_delta || width != s_previous_width || height != s_previous_height) {
    heap_caps_free(s_previous);
    s_previous = NULL;
  }
  frame = heap_caps_calloc((size_t)width * height, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (frame == NULL) {
    ui_release();
    ESP_LOGE(TAG, "No PSRAM for screenshot");
    goto cleanup;
  }
  s_frame = frame;
  s_width = width;
  s_height = height;
  memset(s_cov, 0, sizeof(s_cov));
  s_capture_done = false;
  if (lv_async_call(capture_refresh, NULL) != LV_RESULT_OK) {
    s_frame = NULL;
    ui_release();
    ESP_LOGE(TAG, "Unable to queue screenshot");
    goto cleanup;
  }
  ui_release();
  bool complete = false;
  int64_t deadline = esp_timer_get_time() + (int64_t)SHOT_MAX_WAIT_MS * 1000;
  while (!complete && esp_timer_get_time() < deadline) {
    vTaskDelay(pdMS_TO_TICKS(SHOT_POLL_MS));
    if (!ui_acquire())
      continue;
    complete = s_capture_done;
    ui_release();
  }
  if (!ui_acquire()) {
    /* A later locked capture cancels the request and frees this retained buffer. */
    ESP_LOGE(TAG, "Capture teardown lock timeout; buffer retained");
    return 1;
  }
  lv_async_call_cancel(capture_refresh, NULL);
  lvgl_glue_capture_end();
  s_frame = NULL;
  for (int y = 0; y < height; y++)
    if (!s_cov[y])
      complete = false;
  ui_release();
  if (!complete) {
    ESP_LOGE(TAG, "Incomplete screenshot; no image emitted");
    goto cleanup;
  }

  const bool is_rle = shot_args.rle->count != 0 || is_delta;
  buffers = heap_caps_calloc(1, sizeof(*buffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buffers == NULL) {
    ESP_LOGE(TAG, "No PSRAM for screenshot encoding");
    goto cleanup;
  }
  if (baud) {
    printf("[[BAUD]] %d\n", baud);
    fflush(stdout);
    if (uart_wait_tx_done(CONFIG_ESP_CONSOLE_UART_NUM, pdMS_TO_TICKS(SHOT_DRAIN_MS)) != ESP_OK)
      goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(SHOT_FAST_GUARD_MS));
    if (uart_set_baudrate(CONFIG_ESP_CONSOLE_UART_NUM, baud) != ESP_OK)
      goto cleanup;
    is_fast = true;
  }
  bool has_base = is_delta && s_previous && (uint32_t)shot_args.delta->ival[0] == s_sequence;
  uint32_t sequence = s_sequence == INT32_MAX ? 1 : s_sequence + 1;
  if (is_delta) {
    printf("[[SHOT]] W=%d H=%d FMT=RGB565_DELTA B=%lu S=%lu C=%08lx\n",
           width,
           height,
           (unsigned long)(has_base ? s_sequence : 0),
           (unsigned long)sequence,
           (unsigned long)esp_rom_crc32_le(0, frame, (size_t)width * height * 2));
  } else {
    printf("[[SHOT]] W=%d H=%d FMT=%s\n", width, height, is_rle ? "RGB565_RLE" : "RGB565");
  }
  printf("[[STRIP]] 0 0 %d %d\n", width - 1, height - 1);
  for (int y = 0; y < height; y++) {
    size_t olen = 0;
    const uint8_t *row = frame + (size_t)y * width * 2;
    size_t row_size = (size_t)width * 2;
    if (has_base && memcmp(row, s_previous + (size_t)y * row_size, row_size) == 0)
      continue;
    if (is_rle) {
      row_size = encode_rle_row(buffers->packed, row, width);
      row = buffers->packed;
    }
    if (mbedtls_base64_encode(
            (unsigned char *)buffers->b64, sizeof(buffers->b64), &olen, row, row_size) != 0) {
      ESP_LOGE(TAG, "Screenshot encoding failed");
      goto cleanup;
    }
    buffers->b64[olen] = '\0';
    if (is_delta)
      printf("%d:", y);
    printf("%s\n", buffers->b64);
    // A slow UART frame spans watchdog intervals. Let waiting log writers run
    // between complete rows instead of repeatedly reacquiring stdout ahead of them.
    vTaskDelay(SHOT_ROW_YIELD_TICKS);
  }
  printf("[[/SHOT]]\n");
  result = 0;
  if (is_delta) {
    heap_caps_free(s_previous);
    s_previous = frame;
    s_previous_width = width;
    s_previous_height = height;
    s_sequence = sequence;
    frame = NULL;
  }
cleanup:
  if (is_fast) {
    printf("[[/FAST]]\n");
    fflush(stdout);
    uart_wait_tx_done(CONFIG_ESP_CONSOLE_UART_NUM, pdMS_TO_TICKS(SHOT_DRAIN_MS));
    vTaskDelay(pdMS_TO_TICKS(SHOT_FAST_GUARD_MS));
    uart_set_baudrate(CONFIG_ESP_CONSOLE_UART_NUM, CONFIG_ESP_CONSOLE_UART_BAUDRATE);
  }
  heap_caps_free(frame);
  heap_caps_free(buffers);
  return result;
}

#define UI_STATE_MAX_NODES  256
#define UI_STATE_MAX_DEPTH  16
#define UI_STATE_TEXT_LEN   80
#define UI_STATE_NO_PARENT  UINT16_MAX
#define UI_STATE_SCROLLABLE 1
#define UI_STATE_VISIBLE    2
#define UI_STATE_HIDDEN     4

typedef struct {
  int16_t x, y, w, h, scroll, extent;
  uint16_t parent;
  uint8_t flags, mode;
  char kind;
  char text[UI_STATE_TEXT_LEN];
} ui_state_node_t;

typedef struct {
  ui_state_node_t *nodes;
  uint16_t count;
  bool truncated;
} ui_state_snapshot_t;

static void
snapshot_object(ui_state_snapshot_t *snapshot, lv_obj_t *obj, uint16_t parent, uint8_t depth) {
  if (snapshot->count >= UI_STATE_MAX_NODES || depth >= UI_STATE_MAX_DEPTH) {
    snapshot->truncated = true;
    return;
  }
  uint16_t index = snapshot->count++;
  ui_state_node_t *node = &snapshot->nodes[index];
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  node->parent = parent;
  node->x = area.x1;
  node->y = area.y1;
  node->w = lv_area_get_width(&area);
  node->h = lv_area_get_height(&area);
  node->scroll = lv_obj_get_scroll_y(obj);
  node->extent = lv_obj_get_scroll_top(obj) + lv_obj_get_scroll_bottom(obj);
  node->flags = (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) ? UI_STATE_SCROLLABLE : 0) |
                (lv_obj_is_visible(obj) ? UI_STATE_VISIBLE : 0) |
                (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) ? UI_STATE_HIDDEN : 0);
  node->kind = 'O';
  if (lv_obj_check_type(obj, &lv_label_class)) {
    node->kind = 'L';
    node->mode = lv_label_get_long_mode(obj);
    strlcpy(node->text, lv_label_get_text(obj), sizeof(node->text));
    for (char *c = node->text; *c != '\0'; ++c)
      if (*c == '\n' || *c == '\r' || *c == '\t')
        *c = ' ';
  }
  uint32_t count = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < count && !snapshot->truncated; ++i)
    snapshot_object(snapshot, lv_obj_get_child(obj, i), index, depth + 1);
}

static int cmd_ui_state(int argc, char **argv) {
  (void)argv;
  if (argc != 1)
    return 1;
  ui_state_snapshot_t snapshot = {0};
  snapshot.nodes = heap_caps_calloc(
      UI_STATE_MAX_NODES, sizeof(*snapshot.nodes), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (snapshot.nodes == NULL) {
    ESP_LOGE(TAG, "No PSRAM for UI snapshot");
    return 1;
  }
  if (!ui_acquire()) {
    free(snapshot.nodes);
    return 1;
  }
  lv_obj_t *screen = lv_screen_active();
  int id = ui_current_screen();
  lv_obj_t *top = lv_layer_top();
  if (top != NULL && lv_obj_get_child_count(top) != 0) {
    lv_obj_update_layout(top);
    snapshot_object(&snapshot, top, UI_STATE_NO_PARENT, 0);
  }
  if (screen != NULL) {
    lv_obj_update_layout(screen);
    snapshot_object(&snapshot, screen, UI_STATE_NO_PARENT, 0);
  }
  ui_release();
  printf("UISTATE %d %u %d\n", id, snapshot.count, snapshot.truncated);
  for (uint16_t i = 0; i < snapshot.count; ++i) {
    const ui_state_node_t *n = &snapshot.nodes[i];
    printf("UIN %u %u %c %u %d %d %d %d %d %d %u %s\n",
           i,
           n->parent,
           n->kind,
           n->flags,
           n->x,
           n->y,
           n->w,
           n->h,
           n->scroll,
           n->extent,
           n->mode,
           n->text);
  }
  printf("UIEND\n");
  free(snapshot.nodes);
  return 0;
}

void register_screen_commands(void) {
  const esp_console_cmd_t state_cmd = {
      .command = "uistate",
      .help = "Read-only UI layout and label snapshot",
      .func = &cmd_ui_state,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&state_cmd));

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
  shot_args.rle = arg_lit0("r", "rle", "Lossless RLE");
  shot_args.baud = arg_int0("b", "baud", "<baud>", "Capture-only UART speed");
  shot_args.delta = arg_int0("d", "delta", "<base>", "Changed rows; 0 requests full frame");
  shot_args.end = arg_end(1);
  const esp_console_cmd_t shot_cmd = {
      .command = "screenshot",
      .help = "Capture: screenshot [id] [-r] [-b baud] [-d base]",
      .func = &cmd_screenshot,
      .argtable = &shot_args,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&shot_cmd));
}
