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

#include "app_switcher_ui.h"

#include <string.h>
#include "misc/cache/instance/lv_image_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "menu_ui.h"
#include "nfc_read_ui.h"
#include "rfid_menu_ui.h"
#include "notify_ui.h"
#include "ui_feedback.h"
#include "ui_chrome.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define TASK_COUNT    5
#define TASK_PAD      12
#define TASK_TITLE_H  38
#define TASK_FOOTER_H 42

static const char *TAG = "TASKS_UI";
typedef struct {
  int app;
  screen_id_t screen;
  int selection;
  bool resumable;
  lv_draw_buf_t preview;
  uint8_t *pixels;
  keyboard_snapshot_t *draft;
} task_entry_t;

static task_entry_t s_tasks[TASK_COUNT];
static int s_count, s_selected;
static int s_owner = -1;
static bool s_resuming;
static int s_resume_app = -1;
static lv_obj_t *s_overlay, *s_cards, *s_title, *s_hint;

static int catalog_app(screen_id_t screen) {
  for (int i = 0; i < menu_catalog_count(); ++i)
    if (menu_catalog_target(i) == screen)
      return i;
  return -1;
}

static bool can_resume(screen_id_t screen) {
  switch (screen) {
    case SCREEN_FILES:
    case SCREEN_IR_SAVED:
    case SCREEN_LORA_MQTT:
    case SCREEN_SETTINGS:
    case SCREEN_DISPLAY_SETTINGS:
    case SCREEN_INTERFACE_SETTINGS:
    case SCREEN_SOUND_SETTINGS:
    case SCREEN_CONNECTION_SETTINGS:
    case SCREEN_BATTERY_SETTINGS:
    case SCREEN_ABOUT_SETTINGS:
    case SCREEN_STORAGE:
    case SCREEN_WIFI_MENU:
    case SCREEN_BLE_MENU:
    case SCREEN_NFC_MENU:
    case SCREEN_IR_MENU:
    case SCREEN_SUBGHZ_MENU:
    case SCREEN_BADUSB_MENU:
    case SCREEN_DEV_MENU:
    case SCREEN_GAMES_MENU:
    case SCREEN_PLAYER:
    case SCREEN_GAME_SNAKE:
    case SCREEN_GAME_BREAKOUT:
      return true;
    case SCREEN_NFC_READ:
      return ui_nfc_read_can_resume();
    case SCREEN_RFID_MENU:
      return ui_rfid_menu_can_resume();
    default:
      return false;
  }
}

static bool draft_supported(screen_id_t screen) {
  return screen == SCREEN_LORA_MQTT || screen == SCREEN_IR_SAVED || screen == SCREEN_RFID_MENU ||
         screen == SCREEN_NFC_READ;
}

static void discard(task_entry_t *entry) {
  keyboard_snapshot_free(entry->draft);
  if (entry->pixels != NULL) {
    lv_image_cache_drop(&entry->preview);
    heap_caps_free(entry->pixels);
  }
  memset(entry, 0, sizeof(*entry));
}

static int find_task(int app) {
  for (int i = 0; i < s_count; ++i)
    if (s_tasks[i].app == app)
      return i;
  return -1;
}

static bool capture(void) {
  if (s_owner < 0)
    return true;
  screen_id_t screen = ui_current_screen();
  bool resumable = can_resume(screen);
  keyboard_snapshot_t *draft = NULL;
  if (keyboard_is_open()) {
    if (!resumable || !draft_supported(screen))
      return false;
    draft = keyboard_snapshot_capture();
    if (draft == NULL)
      return false;
    keyboard_mask_preview();
  }
  lv_obj_t *root = lv_screen_active();
  lv_obj_update_layout(root);
  int width = lv_obj_get_width(root), height = lv_obj_get_height(root);
  uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
  size_t bytes = (size_t)stride * height;
  uint8_t *pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lv_draw_buf_t preview;
  if (pixels == NULL) {
    ESP_LOGE(TAG, "No memory for task preview");
    keyboard_snapshot_free(draft);
    return false;
  }
  lv_draw_buf_init(&preview, width, height, LV_COLOR_FORMAT_RGB565, stride, pixels, bytes);
  if (lv_snapshot_take_to_draw_buf(root, LV_COLOR_FORMAT_RGB565, &preview) != LV_RESULT_OK) {
    heap_caps_free(pixels);
    keyboard_snapshot_free(draft);
    return false;
  }
  for (int i = 0; i < s_count; ++i)
    lv_image_cache_drop(&s_tasks[i].preview);
  int index = find_task(s_owner);
  if (index < 0) {
    if (s_count == TASK_COUNT)
      discard(&s_tasks[--s_count]);
    index = s_count++;
  } else
    discard(&s_tasks[index]);
  for (int i = index; i > 0; --i)
    s_tasks[i] = s_tasks[i - 1];
  s_tasks[0] = (task_entry_t){.app = s_owner,
                              .screen = screen,
                              .resumable = resumable,
                              .selection = menu_component_active_selection(),
                              .preview = preview,
                              .pixels = pixels,
                              .draft = draft};
  return true;
}

static lv_obj_t *text(lv_obj_t *parent, const char *value) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, value);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

static bool entry_can_resume(const task_entry_t *entry) {
  if (!entry->resumable)
    return false;
  if (entry->screen == SCREEN_GAME_SNAKE || entry->screen == SCREEN_GAME_BREAKOUT)
    return entry->preview.header.w == ui_screen_w() && entry->preview.header.h == ui_screen_h();
  return true;
}

static void show_cards(void) {
  lv_obj_clean(s_cards);
  if (s_count == 0) {
    lv_label_set_text(s_title, "RECENT APPS");
    lv_obj_t *empty = text(s_cards, "Your screens will appear here\nOpen an app to get started");
    lv_obj_center(empty);
    lv_label_set_text(s_hint, "BACK Return   DOWN Apps");
    return;
  }
  task_entry_t *entry = &s_tasks[s_selected];
  lv_label_set_text_fmt(
      s_title, "%s   %d/%d", menu_catalog_name(entry->app), s_selected + 1, s_count);
  int width = ui_screen_w() - 2 * TASK_PAD - 20;
  int height = ui_screen_h() - TASK_TITLE_H - TASK_FOOTER_H - 8;
  for (int delta = -1; delta <= 1; ++delta) {
    int index = s_selected + delta;
    if (index < 0 || index >= s_count)
      continue;
    lv_obj_t *card = lv_obj_create(s_cards);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, width, height);
    lv_obj_align(card, LV_ALIGN_CENTER, delta * (width + 10), 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, current_theme.bg_primary, 0);
    lv_obj_set_style_border_color(
        card, delta == 0 ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(card, delta == 0 ? 2 : 1, 0);
    lv_obj_t *image = lv_image_create(card);
    lv_image_set_src(image, &s_tasks[index].preview);
    lv_obj_set_size(image, width - 12, height - 12);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_antialias(image, true);
    lv_obj_center(image);
  }
  bool current = entry->app == s_owner && entry->screen == ui_current_screen();
  lv_label_set_text_fmt(s_hint,
                        "%s   %s\n" LV_SYMBOL_LEFT LV_SYMBOL_RIGHT
                        " Choose   DOWN Apps   BACK Return",
                        current                   ? "OK Return"
                        : entry_can_resume(entry) ? "OK Resume"
                                                  : "OK Reopen",
                        entry->draft              ? "Draft kept"
                        : entry_can_resume(entry) ? "Last view kept"
                                                  : "View not retained");
}

bool ui_tasks_is_open(void) {
  return s_overlay != NULL;
}
bool ui_tasks_is_resuming(void) {
  return s_resuming;
}

void ui_tasks_close(void) {
  if (s_overlay != NULL) {
    ui_input_consume_gesture();
    lv_obj_delete(s_overlay);
  }
  s_overlay = s_cards = s_title = s_hint = NULL;
}

void ui_tasks_reset(void) {
  ui_tasks_close();
  for (int i = 0; i < s_count; ++i)
    discard(&s_tasks[i]);
  s_count = 0;
  s_owner = -1;
}

void ui_tasks_toggle(void) {
  if (s_overlay != NULL) {
    ui_tasks_close();
    return;
  }
  if (ui_current_screen() == SCREEN_APP_RUNNING || ui_current_screen() == SCREEN_USB_STORAGE)
    return;
  if (!capture()) {
    notify(NOTIFY_WARNING,
           keyboard_is_open() ? "Save or cancel this edit first"
                              : "Not enough memory for previews");
    return;
  }
  s_selected = 0;
  s_overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_overlay, ui_screen_w(), ui_screen_h());
  lv_obj_set_pos(s_overlay, 0, 0);
  lv_obj_set_style_pad_all(s_overlay, 0, 0);
  lv_obj_set_style_border_width(s_overlay, 0, 0);
  lv_obj_set_style_radius(s_overlay, 0, 0);
  lv_obj_set_style_bg_color(s_overlay, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
  s_title = text(s_overlay, "RECENT APPS");
  lv_obj_set_width(s_title, ui_screen_w() - 16);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 12);
  s_cards = lv_obj_create(s_overlay);
  lv_obj_set_size(s_cards, ui_screen_w(), ui_screen_h() - TASK_TITLE_H - TASK_FOOTER_H);
  lv_obj_set_pos(s_cards, 0, TASK_TITLE_H);
  lv_obj_set_style_pad_all(s_cards, 0, 0);
  lv_obj_set_style_border_width(s_cards, 0, 0);
  lv_obj_set_style_bg_opa(s_cards, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(s_cards, LV_OBJ_FLAG_SCROLLABLE);
  s_hint = text(s_overlay, "");
  lv_obj_set_width(s_hint, ui_screen_w() - 8);
  lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -5);
  show_cards();
}

void ui_tasks_input(const input_event_t *event) {
  if (!s_overlay || (event->action != INPUT_ACTION_PRESS && event->action != INPUT_ACTION_REPEAT))
    return;
  if (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT) {
    if (s_count == 0)
      return;
    s_selected = (s_selected + s_count + (event->button == INPUT_BTN_RIGHT ? 1 : -1)) % s_count;
    show_cards();
    ui_feedback(UI_FB_NAV);
  } else if (event->action == INPUT_ACTION_PRESS) {
    if (event->button == INPUT_BTN_BACK)
      ui_tasks_close();
    else if (event->button == INPUT_BTN_DOWN) {
      ui_tasks_close();
      ui_switch_screen(SCREEN_MENU);
    } else if (event->button == INPUT_BTN_OK && s_count > 0) {
      task_entry_t target = s_tasks[s_selected];
      bool current = target.app == s_owner && target.screen == ui_current_screen();
      ui_tasks_close();
      if (current)
        return;
      s_resuming = entry_can_resume(&target);
      s_resume_app = target.app;
      ui_switch_screen(s_resuming ? target.screen : menu_catalog_target(target.app));
      s_resuming = false;
      s_resume_app = -1;
    }
  }
}

void ui_tasks_before_switch(screen_id_t from, screen_id_t to) {
  ui_tasks_close();
  int next = catalog_app(to);
  if (s_owner >= 0 && (to == SCREEN_HOME || to == SCREEN_MENU || s_resume_app >= 0 ||
                       (next >= 0 && next != s_owner)))
    (void)capture();
  if (s_resume_app >= 0)
    ui_chrome_set_breadcrumb_root(menu_catalog_name(s_resume_app));
  (void)from;
}

void ui_tasks_after_switch(screen_id_t to) {
  if (s_resume_app >= 0) {
    s_owner = s_resume_app;
    int index = find_task(s_owner);
    if (s_resuming && index >= 0) {
      menu_component_restore_selection(s_tasks[index].selection);
      if (s_tasks[index].draft != NULL) {
        keyboard_snapshot_restore(s_tasks[index].draft);
        if (!keyboard_is_open())
          notify(NOTIFY_WARNING, "Could not restore draft");
      }
    }
  } else if (to == SCREEN_HOME || to == SCREEN_MENU)
    s_owner = -1;
  else {
    int app = catalog_app(to);
    if (app >= 0) {
      int old = find_task(app);
      if (old >= 0) {
        for (int i = 0; i < s_count; ++i)
          lv_image_cache_drop(&s_tasks[i].preview);
        discard(&s_tasks[old]);
        for (int i = old; i + 1 < s_count; ++i)
          s_tasks[i] = s_tasks[i + 1];
        memset(&s_tasks[--s_count], 0, sizeof(s_tasks[0]));
      }
      s_owner = app;
    }
  }
}
