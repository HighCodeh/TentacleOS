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

#include "settings_ui.h"

#include <stdio.h>

#include "esp_log.h"

#include "assets_manager.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_theme.h"
#include "menu_component_ui.h"
#include "reboot_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "c5_flasher.h"
#include "spi_protocol.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

static const char *TAG = "SETTINGS_UI";

#define ENTRY_FADE_MS 180

#define C5_PROGRESS_TICK_MS       200
#define C5_DISMISS_DELAY_MS       2000
#define C5_FLASH_TASK_STACK       8192
#define C5_FLASH_TASK_PRIO        SYS_PRIO_SERVICE_HI
#define C5_ROM_FLASH_TASK_STACK   8192
#define C5_ROM_FLASH_TASK_PRIO    SYS_PRIO_SERVICE_HI
#define C5_PASSTHROUGH_TASK_STACK 8192
#define C5_PASSTHROUGH_TASK_PRIO  SYS_PRIO_SERVICE_HI
#define REBOOT_DELAY_MS           80

#define ACTION_TOGGLE_ROTATION (-1)
#define ACTION_FLASH_C5        (-2)
#define ACTION_C5_PASSTHROUGH  (-3)
#define ACTION_REBOOT_P4       (-4)
#define ACTION_FLASH_C5_ROM    (-5)
#define ACTION_RELEASE_C5_UART (-6)

#define GOTO_LAB (-20)
#define GOTO_DEV (-21)

#define DEV_GRID_WIDTH 216
#define DEV_GRID_COLS  2
#define DEV_TILE_W     100
#define DEV_TILE_H     66
#define DEV_TILE_GAP   8
#define DEV_TILE_RAD   12
#define DEV_DANGER_COL 0xFF5470

typedef enum {
  VIEW_MAIN = 0,
  VIEW_LAB,
  VIEW_DEV,
} settings_view_t;

typedef struct {
  const char *name;
  const char *icon;
  int target;
} settings_item_t;

static const settings_item_t MAIN_ITEMS[] = {
    {"CONNECTION", "/assets/icons/wifi.bin", SCREEN_CONNECTION_SETTINGS},
    {"DISPLAY", "/assets/icons/display_settings.bin", SCREEN_DISPLAY_SETTINGS},
    {"INTERFACE", "/assets/icons/tune.bin", SCREEN_INTERFACE_SETTINGS},
    {"THEME", "/assets/icons/palette.bin", SCREEN_THEME_SELECTOR},
    {"ROTATE SCREEN", "/assets/icons/screen_rotation.bin", ACTION_TOGGLE_ROTATION},
    {"SOUND", "/assets/icons/volume_up.bin", SCREEN_SOUND_SETTINGS},
    {"AUDIO & HAPTICS", "/assets/icons/graphic_eq.bin", GOTO_LAB},
    {"BATTERY", "/assets/icons/battery_full.bin", SCREEN_BATTERY_SETTINGS},
    {"POWER", "/assets/icons/power_settings_new.bin", SCREEN_POWER},
    {"STORAGE", "/assets/icons/storage.bin", SCREEN_STORAGE},
    {"FIRMWARE", "/assets/icons/developer_board.bin", GOTO_DEV},
    {"ABOUT", "/assets/icons/info.bin", SCREEN_ABOUT_SETTINGS},
    {"TIME", "/assets/icons/timer.bin", SCREEN_TIME},
    {"RESTART P4", "/assets/icons/restart_alt.bin", ACTION_REBOOT_P4},
};
#define MAIN_COUNT ((int)(sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0])))

typedef struct {
  int before;
  const char *title;
} settings_section_t;
static const settings_section_t MAIN_SECTIONS[] = {
    {0, "Connectivity"},
    {1, "Interface"},
    {5, "Sound & Haptics"},
    {7, "Power"},
    {9, "System"},
};
#define MAIN_SECTION_COUNT ((int)(sizeof(MAIN_SECTIONS) / sizeof(MAIN_SECTIONS[0])))

static const settings_item_t LAB_ITEMS[] = {
    {"VIBRATION", "/assets/icons/vibration.bin", SCREEN_HAPTIC},
    {"SPEAKER", "/assets/icons/speaker.bin", SCREEN_SPEAKER},
    {"MIC", "/assets/icons/mic.bin", SCREEN_MIC_REC},
    {"SPECTRUM", "/assets/icons/graphic_eq.bin", SCREEN_SPECTRUM},
    {"MOTION / IMU", "/assets/icons/screen_rotation.bin", SCREEN_IMU_MONITOR},
};
#define LAB_COUNT ((int)(sizeof(LAB_ITEMS) / sizeof(LAB_ITEMS[0])))

static const settings_item_t DEV_ITEMS[] = {
    {"UPDATE C5", "/assets/icons/system_update.bin", ACTION_FLASH_C5},
    {"FLASH C5 (ROM)", "/assets/icons/usb.bin", ACTION_FLASH_C5_ROM},
    {"RELEASE UART", "/assets/icons/cable.bin", ACTION_RELEASE_C5_UART},
    {"C5 BRIDGE", "/assets/icons/swap_horiz.bin", ACTION_C5_PASSTHROUGH},
    {"C5 STATUS", "/assets/icons/troubleshoot.bin", SCREEN_C5_STATUS},
};
#define DEV_COUNT ((int)(sizeof(DEV_ITEMS) / sizeof(DEV_ITEMS[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static settings_view_t s_view = VIEW_MAIN;

static lv_obj_t *s_dev_tiles[DEV_COUNT];
static int s_dev_sel = 0;

static lv_obj_t *s_c5_overlay = NULL;
static lv_obj_t *s_c5_status_label = NULL;
static lv_obj_t *s_c5_bar = NULL;
static lv_timer_t *s_c5_prog_timer = NULL;
static bool s_c5_in_progress = false;

static void build_settings_view(settings_view_t view);

static const settings_item_t *
view_table(settings_view_t view, int *count, const char **title, const char **icon) {
  switch (view) {
    case VIEW_LAB:
      if (count)
        *count = LAB_COUNT;
      if (title)
        *title = "AUDIO & HAPTICS";
      if (icon)
        *icon = "/assets/icons/graphic_eq.bin";
      return LAB_ITEMS;
    case VIEW_DEV:
      if (count)
        *count = DEV_COUNT;
      if (title)
        *title = "FIRMWARE";
      if (icon)
        *icon = "/assets/icons/developer_board.bin";
      return DEV_ITEMS;
    case VIEW_MAIN:
    default:
      if (count)
        *count = MAIN_COUNT;
      if (title)
        *title = "SETTINGS";
      if (icon)
        *icon = "/assets/icons/settings.bin";
      return MAIN_ITEMS;
  }
}

static void rotation_confirm_cb(bool confirm) {
  if (confirm)
    ui_manager_relayout_current();
}

static void show_c5_progress(const char *msg) {
  if (s_c5_overlay == NULL) {
    s_c5_overlay = lv_obj_create(s_screen);
    lv_obj_set_size(s_c5_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_c5_overlay);
    lv_obj_remove_flag(s_c5_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_c5_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_c5_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_c5_overlay, 0, 0);

    lv_obj_t *box = lv_obj_create(s_c5_overlay);
    lv_obj_set_size(box, 204, 134);
    lv_obj_center(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 14, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_shadow_width(box, 26, 0);
    lv_obj_set_style_shadow_color(box, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);

    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, LV_SYMBOL_DOWNLOAD "  UPDATING C5");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_c5_status_label = lv_label_create(box);
    lv_label_set_text(s_c5_status_label, msg ? msg : "Starting...");
    lv_obj_set_style_text_color(s_c5_status_label, current_theme.text_main, 0);
    lv_obj_set_style_text_align(s_c5_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_c5_status_label, LV_ALIGN_CENTER, 0, -4);
    lv_label_set_long_mode(s_c5_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_c5_status_label, 180);

    s_c5_bar = lv_bar_create(box);
    lv_obj_set_size(s_c5_bar, 170, 12);
    lv_obj_align(s_c5_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(s_c5_bar, 0, 100);
    lv_bar_set_value(s_c5_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_c5_bar, lv_color_hex(0x202028), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_c5_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_c5_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_c5_bar, lv_color_hex(0x00E676), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_c5_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_c5_bar, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(s_c5_bar, LV_OBJ_FLAG_HIDDEN);
  } else if (s_c5_status_label && msg) {
    lv_label_set_text(s_c5_status_label, msg);
  }
}

static void hide_c5_progress(void) {
  if (s_c5_prog_timer) {
    lv_timer_delete(s_c5_prog_timer);
    s_c5_prog_timer = NULL;
  }
  if (s_c5_overlay) {
    lv_obj_del(s_c5_overlay);
    s_c5_overlay = NULL;
    s_c5_status_label = NULL;
    s_c5_bar = NULL;
  }
  s_c5_in_progress = false;
}

static void c5_flash_done_on_lvgl(void *data) {
  esp_err_t r = (esp_err_t)(intptr_t)data;
  hide_c5_progress();

  if (r == ESP_OK)
    msgbox_open_info("/assets/icons/system_update.bin",
                     "C5 UPDATED",
                     "C5 rebooted into the new firmware.",
                     lv_color_hex(0x00E676));
  else
    msgbox_open_info("/assets/icons/error.bin",
                     "UPDATE FAILED",
                     "Check the serial log for details.",
                     lv_color_hex(0xFF5470));
}

static void c5_flash_task(void *arg) {
  (void)arg;
  esp_err_t init_r = c5_flasher_init();
  esp_err_t r = (init_r != ESP_OK) ? init_r : c5_flasher_update(NULL, 0, SPI_OTA_TRANSPORT_SPI);
  ESP_LOGI(TAG, "c5_flasher result: %s", esp_err_to_name(r));

  lv_async_call(c5_flash_done_on_lvgl, (void *)(intptr_t)r);
  vTaskDelete(NULL);
}

static void c5_progress_tick(lv_timer_t *t) {
  (void)t;
  if (s_c5_bar == NULL)
    return;
  uint32_t sent = 0, total = 0;
  c5_flasher_progress(&sent, &total);
  int pct = total ? (int)((uint64_t)sent * 100 / total) : 0;
  lv_bar_set_value(s_c5_bar, pct, LV_ANIM_OFF);
}

static void start_c5_flash(void) {
  if (s_c5_in_progress)
    return;
  s_c5_in_progress = true;
  show_c5_progress("Updating C5 (OTA)...\nDo not power off.");

  if (s_c5_bar)
    lv_obj_remove_flag(s_c5_bar, LV_OBJ_FLAG_HIDDEN);
  if (s_c5_prog_timer == NULL)
    s_c5_prog_timer = lv_timer_create(c5_progress_tick, C5_PROGRESS_TICK_MS, NULL);

  xTaskCreatePinnedToCore(c5_flash_task,
                          "c5_flash",
                          C5_FLASH_TASK_STACK,
                          NULL,
                          C5_FLASH_TASK_PRIO,
                          NULL,
                          SYS_CORE_RADIO);
}

static void c5_rom_flash_task(void *arg) {
  (void)arg;
  esp_err_t r = c5_flasher_rom_flash();
  ESP_LOGI(TAG, "c5_rom_flash result: %s", esp_err_to_name(r));
  lv_async_call(c5_flash_done_on_lvgl, (void *)(intptr_t)r);
  vTaskDelete(NULL);
}

static void start_c5_rom_flash(void) {
  if (s_c5_in_progress)
    return;
  s_c5_in_progress = true;
  show_c5_progress(
      "ROM flash (blank C5).\nC5 must be in download\nmode. ~2-3 min. Don't\npower off.");
  xTaskCreatePinnedToCore(c5_rom_flash_task,
                          "c5_rom_flash",
                          C5_ROM_FLASH_TASK_STACK,
                          NULL,
                          C5_ROM_FLASH_TASK_PRIO,
                          NULL,
                          SYS_CORE_RADIO);
}

static void c5_passthrough_task(void *arg) {
  (void)arg;
  c5_passthrough_run();
  vTaskDelete(NULL);
}

static void start_c5_passthrough(void) {
  msgbox_open_info("/assets/icons/swap_horiz.bin",
                   "C5 PASSTHROUGH",
                   "Bridge active. Strap C5 GPIO28 to GND, power-cycle, then flash from "
                   "your PC with esptool (chip esp32c5).",
                   current_theme.border_accent);
  xTaskCreatePinnedToCore(c5_passthrough_task,
                          "c5_passthru",
                          C5_PASSTHROUGH_TASK_STACK,
                          NULL,
                          C5_PASSTHROUGH_TASK_PRIO,
                          NULL,
                          SYS_CORE_RADIO);
}

#define SHUT_TICK_MS   150
#define SHUT_LOG_COLOR 0x00E676
#define SHUT_BUF_LEN   360

static const char *SHUTDOWN_STEPS[] = {
    "ui manager",
    "lvgl",
    "audio i2s",
    "led rgb",
    "buttons",
    "sd card",
    "console",
    "c5 bridge",
    "storage",
};
#define SHUTDOWN_STEP_COUNT ((int)(sizeof(SHUTDOWN_STEPS) / sizeof(SHUTDOWN_STEPS[0])))

static lv_obj_t *s_shut_log = NULL;
static int s_shut_i = 0;

static lv_obj_t *make_blank_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

static void shut_tick_cb(lv_timer_t *t) {
  if (s_shut_i < SHUTDOWN_STEP_COUNT) {
    char buf[SHUT_BUF_LEN];
    snprintf(buf, sizeof(buf), "> closing %s", SHUTDOWN_STEPS[s_shut_i]);
    if (s_shut_log != NULL)
      lv_label_set_text(s_shut_log, buf);
    s_shut_i++;
    return;
  }
  lv_timer_delete(t);
  s_shut_log = NULL;
  reboot_ui_reboot();
}

static void start_reboot_p4(void) {
  s_shut_i = 0;
  lv_obj_t *scr = make_blank_screen();

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, LV_SYMBOL_POWER "  Restarting...");
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 44);

  s_shut_log = lv_label_create(scr);
  lv_label_set_text(s_shut_log, "");
  lv_obj_set_style_text_color(s_shut_log, lv_color_hex(SHUT_LOG_COLOR), 0);
  lv_obj_set_style_text_font(s_shut_log, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_shut_log, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_shut_log, LV_ALIGN_TOP_MID, 0, 76);

  ui_screen_load(scr);
  lv_timer_create(shut_tick_cb, SHUT_TICK_MS, NULL);
}

static bool run_action(int target) {
  switch (target) {
    case ACTION_TOGGLE_ROTATION:

      msgbox_open("/assets/icons/screen_rotation.bin",
                  "Switch portrait/landscape now?",
                  "YES",
                  "NO",
                  rotation_confirm_cb);
      return true;
    case ACTION_FLASH_C5:

      start_c5_flash();
      return true;
    case ACTION_FLASH_C5_ROM:

      start_c5_rom_flash();
      return true;
    case ACTION_RELEASE_C5_UART:

      c5_flasher_release_uart();
      msgbox_open_info("/assets/icons/cable.bin",
                       "UART RELEASED",
                       "GPIO38/39 hi-Z. Use an external serial adapter. Reboot P4 to restore.",
                       current_theme.border_accent);
      return true;
    case ACTION_C5_PASSTHROUGH:

      start_c5_passthrough();
      return true;
    case ACTION_REBOOT_P4:

      ESP_LOGW(TAG, "User-requested P4 reboot from Settings.");
      start_reboot_p4();
      return true;
    default:
      if (target >= 0)
        ui_switch_screen((screen_id_t)target);
      return true;
  }
}

static bool dev_is_danger(int idx) {
  return DEV_ITEMS[idx].target == ACTION_REBOOT_P4;
}

static lv_color_t dev_tile_border(int idx, bool selected) {
  if (selected)
    return current_theme.border_accent;
  if (dev_is_danger(idx))
    return lv_color_hex(DEV_DANGER_COL);
  return current_theme.border_inactive;
}

static void update_dev_selection(void) {
  for (int i = 0; i < DEV_COUNT; i++) {
    if (s_dev_tiles[i] == NULL)
      continue;
    bool sel = (i == s_dev_sel);
    lv_obj_set_style_border_color(s_dev_tiles[i], dev_tile_border(i, sel), 0);
    lv_obj_set_style_bg_opa(s_dev_tiles[i], sel ? LV_OPA_COVER : LV_OPA_80, 0);
    if (sel) {
      lv_color_t glow =
          dev_is_danger(i) ? lv_color_hex(DEV_DANGER_COL) : current_theme.border_accent;
      lv_obj_set_style_shadow_width(s_dev_tiles[i], 14, 0);
      lv_obj_set_style_shadow_color(s_dev_tiles[i], glow, 0);
      lv_obj_set_style_shadow_opa(s_dev_tiles[i], LV_OPA_50, 0);
    } else {
      lv_obj_set_style_shadow_width(s_dev_tiles[i], 0, 0);
    }
  }
}

static lv_obj_t *make_dev_tile(lv_obj_t *parent, int idx) {
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_set_size(tile, DEV_TILE_W, DEV_TILE_H);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(tile, DEV_TILE_RAD, 0);
  lv_obj_set_style_bg_color(tile, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_80, 0);
  lv_obj_set_style_border_width(tile, 2, 0);
  lv_obj_set_style_pad_all(tile, 4, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tile, 3, 0);

  lv_image_dsc_t *dsc = assets_get(DEV_ITEMS[idx].icon);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(tile);
    lv_image_set_src(img, dsc);
    lv_obj_set_size(img, 22, 22);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
  }

  lv_obj_t *lbl = lv_label_create(tile);
  lv_label_set_text(lbl, DEV_ITEMS[idx].name);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, DEV_TILE_W - 12);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      lbl, dev_is_danger(idx) ? lv_color_hex(DEV_DANGER_COL) : current_theme.text_main, 0);

  return tile;
}

static void build_dev_grid(void) {
  ui_chrome_header(s_screen, "FIRMWARE", "/assets/icons/developer_board.bin");
  ui_chrome_footer(s_screen, "OK: RUN    BACK: EXIT");

  lv_obj_t *grid = lv_obj_create(s_screen);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, DEV_GRID_WIDTH, LV_SIZE_CONTENT);
  lv_obj_align(grid, LV_ALIGN_CENTER, 0, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(grid, DEV_TILE_GAP, 0);
  lv_obj_set_style_pad_column(grid, DEV_TILE_GAP, 0);

  for (int i = 0; i < DEV_COUNT; i++)
    s_dev_tiles[i] = make_dev_tile(grid, i);

  update_dev_selection();
  lv_obj_fade_in(grid, ENTRY_FADE_MS, 0);
}

static void dev_grid_input(const input_event_t *ev) {
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_c5_overlay != NULL) {
    if (press && ev->button == INPUT_BTN_BACK)
      hide_c5_progress();
    return;
  }

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        build_settings_view(VIEW_MAIN);
      break;
    case INPUT_BTN_RIGHT:
      if (nav) {
        s_dev_sel = (s_dev_sel + 1) % DEV_COUNT;
        update_dev_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_LEFT:
      if (nav) {
        s_dev_sel = (s_dev_sel == 0) ? DEV_COUNT - 1 : s_dev_sel - 1;
        update_dev_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav && s_dev_sel + DEV_GRID_COLS < DEV_COUNT) {
        s_dev_sel += DEV_GRID_COLS;
        update_dev_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && s_dev_sel - DEV_GRID_COLS >= 0) {
        s_dev_sel -= DEV_GRID_COLS;
        update_dev_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press)
        run_action(DEV_ITEMS[s_dev_sel].target);
      break;
    default:
      break;
  }
}

static void settings_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_view == VIEW_DEV) {
    dev_grid_input(ev);
    return;
  }

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        if (s_view != VIEW_MAIN)
          build_settings_view(VIEW_MAIN);
        else
          ui_switch_screen(SCREEN_MENU);
      }
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press) {
        int count = 0;
        const settings_item_t *items = view_table(s_view, &count, NULL, NULL);
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < count) {
          int target = items[sel].target;
          if (target == GOTO_LAB) {
            build_settings_view(VIEW_LAB);
          } else if (target == GOTO_DEV) {
            build_settings_view(VIEW_DEV);
          } else if (!run_action(target)) {
            ui_switch_screen(target);
          }
        }
      }
      break;
    default:
      break;
  }
}

static void build_settings_view(settings_view_t view) {
  lv_obj_t *prev = s_screen;
  s_view = view;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  if (view == VIEW_DEV) {
    s_dev_sel = 0;
    build_dev_grid();
  } else {
    int count = 0;
    const char *title = NULL;
    const char *icon = NULL;
    const settings_item_t *items = view_table(view, &count, &title, &icon);

    s_menu = menu_component_create(s_screen, title, icon);
    for (int i = 0; i < count; i++) {
      if (view == VIEW_MAIN) {
        for (int s = 0; s < MAIN_SECTION_COUNT; s++)
          if (MAIN_SECTIONS[s].before == i)
            menu_component_add_section(&s_menu, MAIN_SECTIONS[s].title);
      }
      menu_component_add_item(&s_menu, items[i].icon, items[i].name);
    }

    if (s_menu.items_cont != NULL)
      lv_obj_fade_in(s_menu.items_cont, ENTRY_FADE_MS, 0);
  }

  ui_input_set_screen_handler(settings_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
  if (prev != NULL)
    lv_obj_del(prev);
}

void ui_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  build_settings_view(VIEW_MAIN);
}

void ui_settings_open_dev(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  build_settings_view(VIEW_DEV);
}
