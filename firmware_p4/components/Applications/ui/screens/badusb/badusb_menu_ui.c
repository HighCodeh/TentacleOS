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

#include "badusb_menu_ui.h"

#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "bad_usb.h"
#include "ducky_parser.h"
#include "menu_component_ui.h"
#include "sys_prio.h"
#include "tos_flash_paths.h"
#include "tos_storage_paths.h"
#include "tusb_desc.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "BADUSB_UI";

#define TERM_GREEN       0x00E676
#define TERM_DIM_GREEN   0x1F7A52
#define DARK_PANEL_COLOR 0x05090A
#define SIG_GREEN        0x00E676
#define ERR_RED          0xFF5252
#define WARN_AMBER       0xFFB300

#define FADE_MS 200

#define STATUS_Y_OFS (46 + 12)

#define DOT_COUNT      3
#define DOT_SIZE       10
#define DOT_GAP        18
#define DOT_Y_OFS      64
#define DOT_PULSE_MS   480
#define DOT_STAGGER_MS 160

#define STATUS_BLINK_MS 650

#define TERMINAL_W      216
#define TERMINAL_H      138
#define TERMINAL_TOP_Y  84
#define TERMINAL_PAD    8
#define TERMINAL_RADIUS 0
#define TERMINAL_BORDER 2
#define TERM_HEADER_Y   0
#define TERM_BODY_Y     18

#define DELIVERY_W            214
#define DELIVERY_H            36
#define DELIVERY_Y            46
#define DELIVERY_NODE_W       40
#define DELIVERY_NODE_H       30
#define DELIVERY_TRACK_H      2
#define DELIVERY_PACKET       8
#define DELIVERY_PACKET_COUNT 3
#define DELIVERY_TRAVEL_MS    900
#define DELIVERY_STAGGER_MS   300
#define DELIVERY_NODE_COUNT   2

#define PROGRESS_W           214
#define PROGRESS_H           8
#define PROGRESS_Y           252
#define PROGRESS_RADIUS      4
#define PROGRESS_TRACK_COLOR 0x10211A
#define PROGRESS_FULL_PCT    100
#define PCT_LABEL_Y          230
#define PCT_LABEL_BUF        48

#define CONFIRM_Y_OFS -28

#define TERMINAL_BUF_LEN  320
#define TERM_HEAD_LINES   6
#define SCRIPT_LINE_BUF   96
#define PREVIEW_TITLE_BUF 64

#define TERM_PROMPT   "root@target:~#"
#define DETECT_STATUS "Waiting for host"
#define INSTRUCT_TEXT "RIGHT = Run again   BACK = Exit"

#define INFO_PANEL_W       200
#define INFO_PANEL_H       120
#define INFO_PANEL_RADIUS  10
#define INFO_ROW_GAP       24
#define INFO_FIRST_ROW_Y   14
#define INFO_LABEL_X       12
#define STATUS_FOOTER_HINT "BACK exit"

#define PAY_LIST_TOP      46
#define PAY_LIST_W        216
#define PAY_LIST_H        150
#define PAY_ROW_H         26
#define PAY_ROW_GAP       4
#define PAY_KEY_SZ        20
#define PAY_KEY_RADIUS    6
#define PAY_ROW_RADIUS    8
#define PAY_ROW_PAD_HOR   8
#define PAY_ROW_PAD_COL   8
#define PAY_GLOW_W        14
#define PAY_PREVIEW_W     216
#define PAY_PREVIEW_H     92
#define PAY_PREVIEW_BOT   26
#define PAY_PREVIEW_RAD   8
#define PAY_PREVIEW_PAD   8
#define PAY_PREVIEW_BODY_Y 18
#define PAY_KEY_TINT_OPA  LV_OPA_20
#define PAY_BODY_BUF_LEN  200
#define PREVIEW_MAX_LINES 4

#define BADUSB_SCRIPT_DIR TOS_PATH_BADUSB
#define BADUSB_ASSET_DIR  FLASH_STORAGE_BADUSB
#define ASSETS_PREFIX_LEN (sizeof(FLASH_MOUNT "/") - 1)
#define MAX_PAYLOADS      24
#define PL_PATH_LEN       192
#define PL_NAME_LEN       56

#define POLL_MS 80

#define RUN_TASK_STACK 4096

#define BADUSB_MIN_FREE_INTERNAL  45000
#define BADUSB_MIN_BLOCK_INTERNAL 15000

static const struct {
  const char *name;
  const char *icon;
} MENU_ITEMS[] = {
    {"Run Payload", "/assets/icons/play_arrow.bin"},
    {"Payloads", "/assets/icons/description.bin"},
    {"Keyboard Layout", "/assets/icons/keyboard.bin"},
    {"USB Status", "/assets/icons/usb.bin"},
    {"HID Mouse", "/assets/icons/usb.bin"},
};
#define MENU_ITEM_COUNT ((int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])))

#define IDX_RUN_PAYLOAD 0
#define IDX_PAYLOADS    1
#define IDX_LAYOUT      2
#define IDX_STATUS      3
#define IDX_MOUSE       4

static const struct {
  const char *label;
  ducky_layout_t layout;
} LAYOUTS[] = {
    {"US", DUCKY_LAYOUT_US},
    {"BR (ABNT2)", DUCKY_LAYOUT_ABNT2},
};
#define LAYOUT_COUNT ((int)(sizeof(LAYOUTS) / sizeof(LAYOUTS[0])))

typedef enum {
  RUN_STAGE_DETECTING = 0,
  RUN_STAGE_TYPING,
  RUN_STAGE_DONE,
} run_stage_t;

typedef enum {
  VIEW_LIST = 0,
  VIEW_PAYLOADS,
  VIEW_RUNNING,
  VIEW_LAYOUT,
  VIEW_STATUS,
} view_t;

typedef enum {
  BAD_IDLE = 0,
  BAD_WAIT,
  BAD_RUN,
  BAD_DONE,
  BAD_ABORTED,
  BAD_ERROR,
} bad_state_t;

typedef struct {
  const char *label;
  const char *value;
  bool is_accent;
} badusb_status_row_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static view_t s_view = VIEW_LIST;

static int s_payload_sel = 0;
static int s_layout_active = 0;

static char s_pl_path[MAX_PAYLOADS][PL_PATH_LEN];
static char s_pl_name[MAX_PAYLOADS][PL_NAME_LEN];
static bool s_pl_is_asset[MAX_PAYLOADS];
static int s_pl_count = 0;

static lv_obj_t *s_pay_rows[MAX_PAYLOADS];
static lv_obj_t *s_pay_list = NULL;
static lv_obj_t *s_pay_prev_title = NULL;
static lv_obj_t *s_pay_prev_body = NULL;

static run_stage_t s_run_stage = RUN_STAGE_DETECTING;
static lv_obj_t *s_status_lbl = NULL;
static lv_obj_t *s_detect_group = NULL;
static lv_obj_t *s_delivery_group = NULL;
static lv_obj_t *s_term_lbl = NULL;
static lv_obj_t *s_progress = NULL;
static lv_obj_t *s_pct_lbl = NULL;
static lv_timer_t *s_poll_timer = NULL;

static _Atomic bad_state_t s_bad_state = BAD_IDLE;
static _Atomic int s_bad_cur = 0;
static _Atomic int s_bad_total = 0;
static _Atomic esp_err_t s_bad_result = ESP_OK;
static _Atomic bool s_bad_abort = false;
static _Atomic bool s_run_active = false;
static bool s_prev_mux_native = false;
static char s_run_path[PL_PATH_LEN];
static bool s_run_is_asset = false;

static void badusb_input(const input_event_t *ev, void *ctx);
static void build_screen(void);

static void stop_poll_timer(void) {
  if (s_poll_timer != NULL) {
    lv_timer_delete(s_poll_timer);
    s_poll_timer = NULL;
  }
}

static void opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void translate_x_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void fade_in(lv_obj_t *obj, uint32_t duration_ms) {
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, duration_ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static bool is_ducky_script(const char *name) {
  const char *dot = strrchr(name, '.');
  if (dot == NULL)
    return false;
  return strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".dd") == 0 ||
         strcasecmp(dot, ".duck") == 0 || strcasecmp(dot, ".ducky") == 0;
}

static void scan_dir_into(const char *dir, bool is_asset) {
  DIR *d = opendir(dir);
  if (d == NULL) {
    ESP_LOGW(TAG, "No script dir: %s", dir);
    return;
  }
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && s_pl_count < MAX_PAYLOADS) {
    if (ent->d_name[0] == '.')
      continue;
    if (ent->d_type == DT_DIR)
      continue;
    if (!is_ducky_script(ent->d_name))
      continue;
    if (strlen(dir) + 1 + strlen(ent->d_name) >= PL_PATH_LEN)
      continue;
    strlcpy(s_pl_path[s_pl_count], dir, PL_PATH_LEN);
    strlcat(s_pl_path[s_pl_count], "/", PL_PATH_LEN);
    strlcat(s_pl_path[s_pl_count], ent->d_name, PL_PATH_LEN);
    strlcpy(s_pl_name[s_pl_count], ent->d_name, PL_NAME_LEN);
    s_pl_is_asset[s_pl_count] = is_asset;
    s_pl_count++;
  }
  closedir(d);
}

static void scan_payloads(void) {
  s_pl_count = 0;
  scan_dir_into(BADUSB_SCRIPT_DIR, false);
  scan_dir_into(BADUSB_ASSET_DIR, true);
  ESP_LOGI(TAG, "Found %d script(s)", s_pl_count);
}

static void load_script_head(const char *path, char *out_buf, size_t out_sz, int max_lines) {
  out_buf[0] = '\0';
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    strlcpy(out_buf, "(unreadable)", out_sz);
    return;
  }
  size_t pos = 0;
  int lines = 0;
  char line[SCRIPT_LINE_BUF];
  while (lines < max_lines && fgets(line, sizeof(line), f) != NULL) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    int n = snprintf(out_buf + pos, out_sz - pos, "%s%s", pos ? "\n" : "", line);
    if (n < 0)
      break;
    pos += (size_t)n;
    if (pos >= out_sz) {
      pos = out_sz - 1;
      break;
    }
    lines++;
  }
  fclose(f);
  if (pos == 0)
    strlcpy(out_buf, "(empty)", out_sz);
}

static void bad_progress_cb(int current_line, int total_lines) {
  atomic_store(&s_bad_cur, current_line);
  atomic_store(&s_bad_total, total_lines);
}

static bool badusb_abort_requested(void) {
  return atomic_load(&s_bad_abort);
}

static void bad_run_task(void *arg) {
  (void)arg;
  esp_err_t err = ESP_OK;

  assets_manager_evict_cache();
  size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t big_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  ESP_LOGI(TAG, "pre-run internal heap: free=%u largest=%u", (unsigned)free_int, (unsigned)big_int);
  if (free_int < BADUSB_MIN_FREE_INTERNAL || big_int < BADUSB_MIN_BLOCK_INTERNAL) {
    ESP_LOGE(TAG, "Not enough internal RAM for USB; refusing run");
    atomic_store(&s_bad_result, ESP_ERR_NO_MEM);
    atomic_store(&s_bad_state, BAD_ERROR);
    atomic_store(&s_run_active, false);
    vTaskDelete(NULL);
    return;
  }

  s_prev_mux_native = usb_mux_is_native();
  atomic_store(&s_bad_cur, 0);
  atomic_store(&s_bad_total, 0);
  atomic_store(&s_bad_state, BAD_WAIT);

  err = usb_mux_set_native(true);
  if (err == ESP_OK) {
    esp_err_t ie = bad_usb_init();
    if (ie != ESP_OK && ie != ESP_ERR_INVALID_STATE)
      err = ie;
  }

  if (err == ESP_OK) {
    ducky_set_output_mode(DUCKY_OUTPUT_USB);
    ducky_set_layout(LAYOUTS[s_layout_active].layout);
    ducky_set_progress_callback(bad_progress_cb);

    if (bad_usb_wait_for_connection_ex(badusb_abort_requested)) {
      if (!badusb_abort_requested()) {
        atomic_store(&s_bad_state, BAD_RUN);
        if (s_run_is_asset)
          err = ducky_run_from_assets(s_run_path + ASSETS_PREFIX_LEN);
        else
          err = ducky_run_from_sdcard(s_run_path);
      }
    } else if (!badusb_abort_requested()) {
      err = ESP_ERR_TIMEOUT;
    }
    ducky_set_progress_callback(NULL);
  }

  usb_mux_set_native(s_prev_mux_native);

  atomic_store(&s_bad_result, err);
  if (badusb_abort_requested())
    atomic_store(&s_bad_state, BAD_ABORTED);
  else if (err != ESP_OK)
    atomic_store(&s_bad_state, BAD_ERROR);
  else
    atomic_store(&s_bad_state, BAD_DONE);

  atomic_store(&s_run_active, false);
  vTaskDelete(NULL);
}

static void request_abort(void) {
  atomic_store(&s_bad_abort, true);
  ducky_abort();
}

static void build_detecting(void) {
  s_status_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_status_lbl, DETECT_STATUS);
  lv_obj_set_style_text_color(s_status_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, STATUS_Y_OFS);

  lv_anim_t blink;
  lv_anim_init(&blink);
  lv_anim_set_var(&blink, s_status_lbl);
  lv_anim_set_exec_cb(&blink, opa_anim_cb);
  lv_anim_set_values(&blink, LV_OPA_40, LV_OPA_COVER);
  lv_anim_set_duration(&blink, STATUS_BLINK_MS);
  lv_anim_set_playback_duration(&blink, STATUS_BLINK_MS);
  lv_anim_set_repeat_count(&blink, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&blink, lv_anim_path_ease_in_out);
  lv_anim_start(&blink);

  s_detect_group = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_detect_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_detect_group, lv_pct(100), lv_pct(100));
  lv_obj_align(s_detect_group, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(s_detect_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_detect_group, 0, 0);
  lv_obj_set_style_pad_all(s_detect_group, 0, 0);

  waves_create(s_detect_group, LV_ALIGN_CENTER, 0, 0, NULL, "/assets/icons/usb.bin");

  int total_w = DOT_COUNT * DOT_SIZE + (DOT_COUNT - 1) * DOT_GAP;
  int x0 = -(total_w / 2) + DOT_SIZE / 2;
  for (int i = 0; i < DOT_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(s_detect_group);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
    lv_obj_align(dot, LV_ALIGN_CENTER, x0 + i * (DOT_SIZE + DOT_GAP), DOT_Y_OFS);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, current_theme.border_accent, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&a, DOT_PULSE_MS);
    lv_anim_set_playback_duration(&a, DOT_PULSE_MS);
    lv_anim_set_delay(&a, i * DOT_STAGGER_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }
}

static void build_terminal(void) {
  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, TERMINAL_W, TERMINAL_H);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, TERMINAL_TOP_Y);
  lv_obj_set_style_radius(panel, TERMINAL_RADIUS, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(DARK_PANEL_COLOR), 0);
  lv_obj_set_style_border_width(panel, TERMINAL_BORDER, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_70, 0);
  lv_obj_set_style_shadow_color(panel, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_shadow_width(panel, 12, 0);
  lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(panel, TERMINAL_PAD, 0);

  lv_obj_t *prompt = lv_label_create(panel);
  lv_label_set_text(prompt, TERM_PROMPT);
  lv_obj_set_style_text_color(prompt, lv_color_hex(TERM_DIM_GREEN), 0);
  lv_obj_set_style_text_font(prompt, &lv_font_montserrat_12, 0);
  lv_obj_align(prompt, LV_ALIGN_TOP_LEFT, 0, TERM_HEADER_Y);

  s_term_lbl = lv_label_create(panel);
  lv_label_set_text(s_term_lbl, "");
  lv_obj_set_width(s_term_lbl, TERMINAL_W - TERMINAL_PAD * 2);
  lv_label_set_long_mode(s_term_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(s_term_lbl, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_term_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_term_lbl, LV_ALIGN_TOP_LEFT, 0, TERM_BODY_Y);

  s_pct_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_pct_lbl, "Executing  0/0");
  lv_obj_set_style_text_color(s_pct_lbl, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_pct_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, 0, PCT_LABEL_Y);

  s_progress = lv_bar_create(s_screen);
  lv_obj_set_size(s_progress, PROGRESS_W, PROGRESS_H);
  lv_obj_align(s_progress, LV_ALIGN_TOP_MID, 0, PROGRESS_Y);
  lv_bar_set_range(s_progress, 0, PROGRESS_FULL_PCT);
  lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_progress, lv_color_hex(PROGRESS_TRACK_COLOR), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_progress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_progress, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_progress, lv_color_hex(TERM_DIM_GREEN), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_progress, lv_color_hex(TERM_DIM_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(s_progress, lv_color_hex(TERM_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(s_progress, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_progress, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_progress, PROGRESS_RADIUS, LV_PART_MAIN);
  lv_obj_set_style_radius(s_progress, PROGRESS_RADIUS, LV_PART_INDICATOR);
}

static void build_delivery(void) {
  s_delivery_group = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_delivery_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_delivery_group, DELIVERY_W, DELIVERY_H);
  lv_obj_align(s_delivery_group, LV_ALIGN_TOP_MID, 0, DELIVERY_Y);
  lv_obj_set_style_bg_opa(s_delivery_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_delivery_group, 0, 0);
  lv_obj_set_style_pad_all(s_delivery_group, 0, 0);

  int track_x0 = DELIVERY_NODE_W;
  int track_x1 = DELIVERY_W - DELIVERY_NODE_W;
  int track_len = track_x1 - track_x0;

  lv_obj_t *track = lv_obj_create(s_delivery_group);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(track, track_len, DELIVERY_TRACK_H);
  lv_obj_align(track, LV_ALIGN_LEFT_MID, track_x0, 0);
  lv_obj_set_style_border_width(track, 0, 0);
  lv_obj_set_style_radius(track, 1, 0);
  lv_obj_set_style_bg_color(track, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_30, 0);

  const char *node_labels[DELIVERY_NODE_COUNT] = {"HID", "HOST"};
  for (int n = 0; n < DELIVERY_NODE_COUNT; n++) {
    lv_obj_t *node = lv_obj_create(s_delivery_group);
    lv_obj_remove_flag(node, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(node, DELIVERY_NODE_W, DELIVERY_NODE_H);
    lv_obj_align(node, n == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(node, 4, 0);
    lv_obj_set_style_pad_all(node, 0, 0);
    lv_obj_set_style_bg_color(node, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(node, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(node, 1, 0);
    lv_obj_set_style_border_color(node, current_theme.border_accent, 0);

    lv_obj_t *lbl = lv_label_create(node);
    lv_label_set_text(lbl, node_labels[n]);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);
  }

  for (int i = 0; i < DELIVERY_PACKET_COUNT; i++) {
    lv_obj_t *pkt = lv_obj_create(s_delivery_group);
    lv_obj_remove_flag(pkt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pkt, DELIVERY_PACKET, DELIVERY_PACKET);
    lv_obj_align(pkt, LV_ALIGN_LEFT_MID, track_x0, 0);
    lv_obj_set_style_radius(pkt, 2, 0);
    lv_obj_set_style_border_width(pkt, 0, 0);
    lv_obj_set_style_bg_color(pkt, lv_color_hex(TERM_GREEN), 0);
    lv_obj_set_style_bg_opa(pkt, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(pkt, lv_color_hex(TERM_GREEN), 0);
    lv_obj_set_style_shadow_width(pkt, 6, 0);
    lv_obj_set_style_shadow_opa(pkt, LV_OPA_50, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pkt);
    lv_anim_set_exec_cb(&a, translate_x_cb);
    lv_anim_set_values(&a, 0, track_len - DELIVERY_PACKET);
    lv_anim_set_duration(&a, DELIVERY_TRAVEL_MS);
    lv_anim_set_delay(&a, i * DELIVERY_STAGGER_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }
}

static void enter_stage_typing(void) {
  if (s_detect_group != NULL) {
    lv_obj_del(s_detect_group);
    s_detect_group = NULL;
  }
  if (s_status_lbl != NULL) {
    lv_obj_del(s_status_lbl);
    s_status_lbl = NULL;
  }

  build_delivery();
  build_terminal();

  char head[TERMINAL_BUF_LEN];
  load_script_head(s_run_path, head, sizeof(head), TERM_HEAD_LINES);
  if (s_term_lbl != NULL)
    lv_label_set_text(s_term_lbl, head);
  fade_in(s_term_lbl, FADE_MS);
}

static void update_progress(void) {
  int cur = atomic_load(&s_bad_cur);
  int total = atomic_load(&s_bad_total);
  int pct = (total > 0) ? (cur * PROGRESS_FULL_PCT / total) : 0;
  if (pct > PROGRESS_FULL_PCT)
    pct = PROGRESS_FULL_PCT;
  if (s_progress != NULL)
    lv_bar_set_value(s_progress, pct, LV_ANIM_OFF);
  if (s_pct_lbl != NULL) {
    char buf[PCT_LABEL_BUF];
    snprintf(buf, sizeof(buf), "Executing  %d/%d", cur, total);
    lv_label_set_text(s_pct_lbl, buf);
  }
}

static void show_done(bad_state_t final) {
  if (s_detect_group != NULL) {
    lv_obj_del(s_detect_group);
    s_detect_group = NULL;
  }
  if (s_status_lbl != NULL) {
    lv_obj_del(s_status_lbl);
    s_status_lbl = NULL;
  }
  if (s_delivery_group != NULL) {
    lv_obj_del(s_delivery_group);
    s_delivery_group = NULL;
  }

  const char *msg;
  uint32_t col;
  if (final == BAD_ERROR) {
    msg = (atomic_load(&s_bad_result) == ESP_ERR_NO_MEM) ? LV_SYMBOL_WARNING "  Low memory"
                                                         : LV_SYMBOL_WARNING "  Run failed";
    col = ERR_RED;
  } else if (final == BAD_ABORTED) {
    msg = LV_SYMBOL_CLOSE "  Aborted";
    col = WARN_AMBER;
  } else {
    msg = LV_SYMBOL_OK "  Payload delivered";
    col = TERM_GREEN;
    if (s_progress != NULL)
      lv_bar_set_value(s_progress, PROGRESS_FULL_PCT, LV_ANIM_ON);
    if (s_pct_lbl != NULL)
      lv_label_set_text(s_pct_lbl, "Executing  done");
  }

  lv_obj_t *confirm = lv_label_create(s_screen);
  lv_label_set_text(confirm, msg);
  lv_obj_set_style_text_color(confirm, lv_color_hex(col), 0);
  lv_obj_set_style_text_font(confirm, &lv_font_montserrat_14, 0);
  lv_obj_align(confirm, LV_ALIGN_BOTTOM_MID, 0, CONFIRM_Y_OFS);
  fade_in(confirm, FADE_MS);

  ui_chrome_footer(s_screen, INSTRUCT_TEXT);

  ESP_LOGI(TAG,
           "payload run %s: %s",
           final == BAD_DONE ? "done" : (final == BAD_ABORTED ? "aborted" : "failed"),
           s_run_path);
  ui_feedback(final == BAD_DONE ? UI_FB_WRITE : UI_FB_SELECT);
}

static void bad_poll_cb(lv_timer_t *t) {
  (void)t;
  if (lv_screen_active() != s_screen || s_view != VIEW_RUNNING) {
    stop_poll_timer();
    return;
  }

  bad_state_t st = atomic_load(&s_bad_state);

  if (st == BAD_RUN) {
    if (s_run_stage == RUN_STAGE_DETECTING) {
      s_run_stage = RUN_STAGE_TYPING;
      enter_stage_typing();
    }
    update_progress();
  } else if (st == BAD_DONE || st == BAD_ABORTED || st == BAD_ERROR) {
    if (s_run_stage != RUN_STAGE_DONE) {
      if (s_run_stage == RUN_STAGE_DETECTING && st == BAD_DONE)
        enter_stage_typing();
      s_run_stage = RUN_STAGE_DONE;
      show_done(st);
    }
    stop_poll_timer();
  }
}

static void build_running(void) {
  ui_chrome_header(s_screen, "BADUSB", "/assets/icons/usb.bin");

  s_run_stage = RUN_STAGE_DETECTING;
  build_detecting();

  if (!atomic_load(&s_run_active)) {
    atomic_store(&s_bad_abort, false);
    atomic_store(&s_bad_cur, 0);
    atomic_store(&s_bad_total, 0);
    atomic_store(&s_bad_state, BAD_IDLE);
    atomic_store(&s_run_active, true);
    BaseType_t ok = xTaskCreatePinnedToCore(
        bad_run_task, "badusb_run", RUN_TASK_STACK, NULL, SYS_PRIO_SERVICE_HI, NULL, SYS_CORE_RADIO);
    if (ok != pdPASS) {
      atomic_store(&s_run_active, false);
      atomic_store(&s_bad_result, ESP_ERR_NO_MEM);
      atomic_store(&s_bad_state, BAD_ERROR);
      ESP_LOGE(TAG, "Failed to spawn run task");
    }
  }

  s_poll_timer = lv_timer_create(bad_poll_cb, POLL_MS, NULL);
}

static void pay_style_row(lv_obj_t *row, bool selected) {
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  if (selected) {
    lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
    lv_obj_set_style_border_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, PAY_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(row, -2, 0);
  } else {
    lv_obj_set_style_bg_color(row, current_theme.bg_primary, 0);
    lv_obj_set_style_border_color(row, current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
  }
}

static void pay_update_preview(int idx) {
  if (idx < 0 || idx >= s_pl_count)
    return;
  if (s_pay_prev_title != NULL) {
    char title[PREVIEW_TITLE_BUF];
    snprintf(title, sizeof(title), "// %s", s_pl_name[idx]);
    lv_label_set_text(s_pay_prev_title, title);
  }
  if (s_pay_prev_body != NULL) {
    char body[PAY_BODY_BUF_LEN];
    load_script_head(s_pl_path[idx], body, sizeof(body), PREVIEW_MAX_LINES);
    lv_label_set_text(s_pay_prev_body, body);
  }
}

static void pay_apply_sel(int idx) {
  for (int i = 0; i < s_pl_count; i++)
    if (s_pay_rows[i] != NULL)
      pay_style_row(s_pay_rows[i], i == idx);
  if (idx >= 0 && idx < s_pl_count && s_pay_rows[idx] != NULL)
    lv_obj_scroll_to_view(s_pay_rows[idx], LV_ANIM_ON);
  pay_update_preview(idx);
}

static void build_payloads_empty(void) {
  ui_chrome_header(s_screen, "PAYLOADS", "/assets/icons/description.bin");
  ui_chrome_footer(s_screen, "BACK back");

  lv_obj_t *icon = lv_label_create(s_screen);
  lv_label_set_text(icon, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_color(icon, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, -24);

  lv_obj_t *msg = lv_label_create(s_screen);
  lv_label_set_text(msg, "No scripts found");
  lv_obj_set_style_text_color(msg, current_theme.text_main, 0);
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
  lv_obj_align(msg, LV_ALIGN_CENTER, 0, 2);

  lv_obj_t *sub = lv_label_create(s_screen);
  lv_label_set_text(sub, "Add .txt/.duck to\n" BADUSB_SCRIPT_DIR);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(sub, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 34);
  fade_in(sub, FADE_MS);
}

static void build_payloads(void) {
  if (s_pl_count <= 0) {
    build_payloads_empty();
    return;
  }

  ui_chrome_header(s_screen, "PAYLOADS", "/assets/icons/description.bin");
  ui_chrome_footer(s_screen, "UP/DOWN pick   OK run   BACK back");

  if (s_payload_sel < 0)
    s_payload_sel = 0;
  if (s_payload_sel >= s_pl_count)
    s_payload_sel = s_pl_count - 1;

  s_pay_list = lv_obj_create(s_screen);
  lv_obj_set_size(s_pay_list, PAY_LIST_W, PAY_LIST_H);
  lv_obj_align(s_pay_list, LV_ALIGN_TOP_MID, 0, PAY_LIST_TOP);
  lv_obj_set_style_bg_opa(s_pay_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_pay_list, 0, 0);
  lv_obj_set_style_pad_all(s_pay_list, 0, 0);
  lv_obj_set_style_pad_row(s_pay_list, PAY_ROW_GAP, 0);
  lv_obj_set_flex_flow(s_pay_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      s_pay_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(s_pay_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_pay_list, LV_SCROLLBAR_MODE_AUTO);

  for (int i = 0; i < s_pl_count; i++) {
    lv_obj_t *row = lv_obj_create(s_pay_list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), PAY_ROW_H);
    lv_obj_set_style_radius(row, PAY_ROW_RADIUS, 0);
    lv_obj_set_style_pad_hor(row, PAY_ROW_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, PAY_ROW_PAD_COL, 0);

    lv_obj_t *key = lv_obj_create(row);
    lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(key, PAY_KEY_SZ, PAY_KEY_SZ);
    lv_obj_set_style_radius(key, PAY_KEY_RADIUS, 0);
    lv_obj_set_style_pad_all(key, 0, 0);
    lv_obj_set_style_border_width(key, 0, 0);
    lv_obj_set_style_bg_color(key, current_theme.border_accent, 0);
    lv_obj_set_style_bg_opa(key, PAY_KEY_TINT_OPA, 0);
    lv_obj_t *kico = lv_label_create(key);
    lv_label_set_text(kico, LV_SYMBOL_FILE);
    lv_obj_set_style_text_color(kico, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(kico, &lv_font_montserrat_12, 0);
    lv_obj_center(kico);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, s_pl_name[i]);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name, current_theme.text_main, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(name, 1);

    s_pay_rows[i] = row;
  }

  lv_obj_t *prev = lv_obj_create(s_screen);
  lv_obj_remove_flag(prev, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(prev, PAY_PREVIEW_W, PAY_PREVIEW_H);
  lv_obj_align(prev, LV_ALIGN_BOTTOM_MID, 0, -PAY_PREVIEW_BOT);
  lv_obj_set_style_radius(prev, PAY_PREVIEW_RAD, 0);
  lv_obj_set_style_bg_color(prev, lv_color_hex(DARK_PANEL_COLOR), 0);
  lv_obj_set_style_bg_opa(prev, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(prev, TERMINAL_BORDER, 0);
  lv_obj_set_style_border_color(prev, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_border_opa(prev, LV_OPA_70, 0);
  lv_obj_set_style_shadow_color(prev, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_shadow_width(prev, 12, 0);
  lv_obj_set_style_shadow_opa(prev, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(prev, PAY_PREVIEW_PAD, 0);

  s_pay_prev_title = lv_label_create(prev);
  lv_obj_set_style_text_color(s_pay_prev_title, lv_color_hex(TERM_DIM_GREEN), 0);
  lv_obj_set_style_text_font(s_pay_prev_title, &lv_font_montserrat_12, 0);
  lv_obj_align(s_pay_prev_title, LV_ALIGN_TOP_LEFT, 0, 0);

  s_pay_prev_body = lv_label_create(prev);
  lv_obj_set_width(s_pay_prev_body, PAY_PREVIEW_W - PAY_PREVIEW_PAD * 2);
  lv_label_set_long_mode(s_pay_prev_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(s_pay_prev_body, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_pay_prev_body, &lv_font_montserrat_12, 0);
  lv_obj_align(s_pay_prev_body, LV_ALIGN_TOP_LEFT, 0, PAY_PREVIEW_BODY_Y);

  pay_apply_sel(s_payload_sel);

  fade_in(s_pay_list, FADE_MS);
  fade_in(prev, FADE_MS);
}

static void build_layout(void) {
  s_menu = menu_component_create(s_screen, "LAYOUT", "/assets/icons/keyboard.bin");
  for (int i = 0; i < LAYOUT_COUNT; i++) {
    menu_component_add_item(&s_menu, "/assets/icons/keyboard.bin", LAYOUTS[i].label);
    if (i == s_layout_active)
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(SIG_GREEN));
  }
  menu_component_select(&s_menu, s_layout_active);
  fade_in(s_menu.items_cont, FADE_MS);
  fade_in(s_menu.title_bar, FADE_MS);
}

static void status_row(lv_obj_t *panel, int index, const badusb_status_row_t *row) {
  lv_obj_t *lab = lv_label_create(panel);
  lv_label_set_text(lab, row->label);
  lv_obj_set_style_text_color(lab, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(lab, &lv_font_montserrat_12, 0);
  lv_obj_align(lab, LV_ALIGN_TOP_LEFT, INFO_LABEL_X, INFO_FIRST_ROW_Y + index * INFO_ROW_GAP);

  lv_obj_t *val = lv_label_create(panel);
  lv_label_set_text(val, row->value);
  lv_obj_set_style_text_color(
      val, row->is_accent ? lv_color_hex(SIG_GREEN) : current_theme.text_main, 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -INFO_LABEL_X, INFO_FIRST_ROW_Y + index * INFO_ROW_GAP);
}

static void build_status(void) {
  ui_chrome_header(s_screen, "USB STATUS", "/assets/icons/usb.bin");
  ui_chrome_footer(s_screen, STATUS_FOOTER_HINT);

  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, INFO_PANEL_W, INFO_PANEL_H);
  lv_obj_align(panel, LV_ALIGN_CENTER, 0, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2);
  lv_obj_set_style_radius(panel, INFO_PANEL_RADIUS, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(panel, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);

  bool native = usb_mux_is_native();
  bool busy = atomic_load(&s_run_active);
  const badusb_status_row_t rows[] = {
      {"USB", "HID + CDC", false},
      {"VID:PID", "CAFE:4001", false},
      {"Mux", native ? "Native USB" : "UART bridge", native},
      {"State", busy ? "Running" : "Ready", !busy},
  };
  for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++)
    status_row(panel, i, &rows[i]);

  fade_in(panel, FADE_MS);
}

static void build_screen(void) {
  stop_poll_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_lbl = NULL;
  s_detect_group = NULL;
  s_delivery_group = NULL;
  s_term_lbl = NULL;
  s_progress = NULL;
  s_pct_lbl = NULL;
  s_pay_prev_title = NULL;
  s_pay_prev_body = NULL;
  s_pay_list = NULL;
  for (int i = 0; i < MAX_PAYLOADS; i++)
    s_pay_rows[i] = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_view) {
    case VIEW_PAYLOADS:
      build_payloads();
      break;
    case VIEW_RUNNING:
      build_running();
      break;
    case VIEW_LAYOUT:
      build_layout();
      break;
    case VIEW_STATUS:
      build_status();
      break;
    case VIEW_LIST:
    default:
      s_menu = menu_component_create(s_screen, "BADUSB", "/assets/icons/usb.bin");
      for (int i = 0; i < MENU_ITEM_COUNT; i++)
        menu_component_add_item(&s_menu, MENU_ITEMS[i].icon, MENU_ITEMS[i].name);
      fade_in(s_menu.items_cont, FADE_MS);
      fade_in(s_menu.title_bar, FADE_MS);
      break;
  }

  ui_input_set_screen_handler(badusb_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void input_view_list(const input_event_t *ev, bool press, bool nav) {
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_OK:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel == IDX_RUN_PAYLOAD) {
          scan_payloads();
          if (s_pl_count <= 0) {
            s_view = VIEW_PAYLOADS;
          } else {
            if (s_payload_sel < 0 || s_payload_sel >= s_pl_count)
              s_payload_sel = 0;
            strlcpy(s_run_path, s_pl_path[s_payload_sel], sizeof(s_run_path));
            s_run_is_asset = s_pl_is_asset[s_payload_sel];
            s_view = VIEW_RUNNING;
          }
          build_screen();
        } else if (sel == IDX_PAYLOADS) {
          scan_payloads();
          s_view = VIEW_PAYLOADS;
          build_screen();
        } else if (sel == IDX_LAYOUT) {
          s_view = VIEW_LAYOUT;
          build_screen();
        } else if (sel == IDX_STATUS) {
          s_view = VIEW_STATUS;
          build_screen();
        } else if (sel == IDX_MOUSE) {
          ui_switch_screen(SCREEN_USB_MOUSE);
        }
      }
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    default:
      break;
  }
}

static void input_view_payloads(const input_event_t *ev, bool press, bool nav) {
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav && s_payload_sel < s_pl_count - 1) {
        s_payload_sel++;
        pay_apply_sel(s_payload_sel);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && s_payload_sel > 0) {
        s_payload_sel--;
        pay_apply_sel(s_payload_sel);
      }
      break;
    case INPUT_BTN_OK:
      if (press && s_pl_count > 0) {
        strlcpy(s_run_path, s_pl_path[s_payload_sel], sizeof(s_run_path));
        s_run_is_asset = s_pl_is_asset[s_payload_sel];
        s_view = VIEW_RUNNING;
        build_screen();
      }
      break;
    case INPUT_BTN_BACK:
      if (press) {
        s_view = VIEW_LIST;
        build_screen();
      }
      break;
    default:
      break;
  }
}

static void input_view_running(const input_event_t *ev, bool press) {
  switch (ev->button) {
    case INPUT_BTN_RIGHT:
      if (press && s_run_stage == RUN_STAGE_DONE && !atomic_load(&s_run_active))
        build_screen();
      break;
    case INPUT_BTN_BACK:
      if (press) {
        request_abort();
        s_view = VIEW_LIST;
        build_screen();
      }
      break;
    default:
      break;
  }
}

static void input_view_layout(const input_event_t *ev, bool press, bool nav) {
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_OK:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < LAYOUT_COUNT && sel != s_layout_active) {
          menu_component_set_item_label_color(&s_menu, s_layout_active, current_theme.text_main);
          s_layout_active = sel;
          menu_component_set_item_label_color(&s_menu, s_layout_active, lv_color_hex(SIG_GREEN));
          ESP_LOGI(TAG, "layout set: %s", LAYOUTS[s_layout_active].label);
        }
      }
      break;
    case INPUT_BTN_BACK:
      if (press) {
        s_view = VIEW_LIST;
        build_screen();
      }
      break;
    default:
      break;
  }
}

static void input_view_status(const input_event_t *ev, bool press) {
  if (ev->button == INPUT_BTN_BACK && press) {
    s_view = VIEW_LIST;
    build_screen();
  }
}

static void badusb_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_view) {
    case VIEW_LIST:
      input_view_list(ev, press, nav);
      break;
    case VIEW_PAYLOADS:
      input_view_payloads(ev, press, nav);
      break;
    case VIEW_RUNNING:
      input_view_running(ev, press);
      break;
    case VIEW_LAYOUT:
      input_view_layout(ev, press, nav);
      break;
    case VIEW_STATUS:
      input_view_status(ev, press);
      break;
    default:
      break;
  }
}

void ui_badusb_menu_open(void) {
  s_poll_timer = NULL;
  s_view = VIEW_LIST;
  s_payload_sel = 0;
  scan_payloads();
  build_screen();
}
