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

#include "keyboard_ui.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "led_control.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define DEFAULT_LIMIT 64
#define KEY_COUNT     32
#define MAP_SIZE      45
#define HEX_COUNT     16
#define FIELD_Y       34
#define FIELD_H       36
#define KEYS_Y        96
#define FOOTER_H      24

static const char *TAG = "KEYBOARD_UI";
static lv_obj_t *s_screen, *s_keys, *s_text, *s_count, *s_title;
static keyboard_submit_cb_t s_submit;
static void *s_context;
static keyboard_config_t s_config;
static unsigned s_page;
static char s_chars[KEY_COUNT][2];
static const char *s_map[MAP_SIZE];
static lv_buttonmatrix_ctrl_t s_controls[KEY_COUNT + 7];
static const char *const s_pages[] = {"abcdefghijklmnopqrstuvwxyz.,@_-/",
                                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,@_-/",
                                      "0123456789.,+-*/=:_@#%&?!()$'\"\\|",
                                      "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"};

extern lv_group_t *main_group;

static uint32_t character_count(const char *text) {
  uint32_t count = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
    if ((*p & 0xC0) != 0x80)
      ++count;
  return count;
}

static void refresh_count(void) {
  const uint32_t count = character_count(lv_textarea_get_text(s_text));
  lv_label_set_text_fmt(s_count,
                        "%lu / %lu%s",
                        (unsigned long)count,
                        (unsigned long)s_config.max_chars,
                        count == s_config.max_chars ? "  - limit reached" : "");
}

static void set_map(void) {
  const bool is_hex = s_config.mode == KEYBOARD_HEX;
  const unsigned count = is_hex ? HEX_COUNT : KEY_COUNT;
  const unsigned columns = is_hex ? 4 : 8;
  const char *chars = is_hex ? "0123456789ABCDEF" : s_pages[s_page];
  unsigned pos = 0;
  for (unsigned i = 0; i < count; ++i) {
    s_chars[i][0] = chars[i];
    s_map[pos++] = s_chars[i];
    s_controls[i] = 1;
    if ((i + 1) % columns == 0)
      s_map[pos++] = "\n";
  }
  unsigned action = count;
  if (!is_hex) {
    s_map[pos++] = s_page == 0 ? "ABC" : "abc";
    s_controls[action++] = 2 | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
    s_map[pos++] = s_page == 2 ? "#+" : "123";
    s_controls[action++] = 2 | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
  }
  s_map[pos++] = LV_SYMBOL_LEFT;
  s_controls[action++] = 1;
  if (!is_hex) {
    s_map[pos++] = "space";
    s_controls[action++] = 3;
  }
  s_map[pos++] = LV_SYMBOL_RIGHT;
  s_controls[action++] = 1;
  s_map[pos++] = LV_SYMBOL_BACKSPACE;
  s_controls[action++] = 2;
  s_map[pos++] = LV_SYMBOL_OK;
  s_controls[action++] = 2 | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
  s_map[pos] = "";
  lv_buttonmatrix_set_map(s_keys, s_map);
  lv_buttonmatrix_set_ctrl_map(s_keys, s_controls);
  lv_buttonmatrix_set_selected_button(s_keys, 0);
}

static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  s_screen = s_keys = s_text = s_count = s_title = NULL;
  s_submit = NULL;
  s_context = NULL;
}

static void submit(void) {
  const char *text = lv_textarea_get_text(s_text);
  const uint32_t length = character_count(text);
  if (length < s_config.min_chars || (s_config.mode == KEYBOARD_HEX && length % 2 != 0)) {
    if (length < s_config.min_chars)
      lv_label_set_text_fmt(
          s_count, "Enter at least %lu characters", (unsigned long)s_config.min_chars);
    else
      lv_label_set_text(s_count, "Enter complete hex bytes");
    ESP_LOGW(TAG, "Input length rejected");
    led_signal_warning();
    return;
  }
  if (s_config.mode == KEYBOARD_FILENAME &&
      (strlen(text) > s_config.max_chars || strpbrk(text, "/\\:*?\"<>|") != NULL ||
       text[0] == '.' || text[0] == ' ' || length == 0 || text[strlen(text) - 1] == '.' ||
       text[strlen(text) - 1] == ' ')) {
    lv_label_set_text(s_count, "Invalid file name");
    led_signal_warning();
    return;
  }
  char *copy = heap_caps_malloc(strlen(text) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (copy == NULL) {
    ESP_LOGE(TAG, "Submit copy allocation failed");
    lv_label_set_text(s_count, "Memory busy - try again");
    led_signal_error();
    return;
  }
  strcpy(copy, text);
  keyboard_submit_cb_t callback = s_submit;
  void *context = s_context;
  // Detach before callbacks can navigate, destroy the parent, or open another editor.
  keyboard_close();
  if (callback != NULL)
    callback(copy, context);
  heap_caps_free(copy);
}

static void key_event(lv_event_t *event) {
  if (lv_event_get_target(event) != s_keys)
    return;
  uint32_t selected = lv_buttonmatrix_get_selected_button(s_keys);
  const char *key = lv_buttonmatrix_get_button_text(s_keys, selected);
  if (key == NULL)
    return;
  if (strcmp(key, LV_SYMBOL_OK) == 0) {
    submit();
    return;
  }
  if (strcmp(key, "ABC") == 0 || strcmp(key, "abc") == 0 || strcmp(key, "123") == 0 ||
      strcmp(key, "#+") == 0) {
    s_page = strcmp(key, "ABC") == 0   ? 1
             : strcmp(key, "abc") == 0 ? 0
             : strcmp(key, "123") == 0 ? 2
                                       : 3;
    set_map();
    return;
  }
  if (strcmp(key, LV_SYMBOL_LEFT) == 0)
    lv_textarea_cursor_left(s_text);
  else if (strcmp(key, LV_SYMBOL_RIGHT) == 0)
    lv_textarea_cursor_right(s_text);
  else if (strcmp(key, LV_SYMBOL_BACKSPACE) == 0)
    lv_textarea_delete_char(s_text);
  else
    lv_textarea_add_text(s_text, strcmp(key, "space") == 0 ? " " : key);
  refresh_count();
}

static lv_obj_t *label(const char *text, int y) {
  lv_obj_t *object = lv_label_create(s_screen);
  lv_label_set_text(object, text);
  lv_obj_set_width(object, ui_screen_w() - 16);
  lv_obj_set_style_text_color(object, current_theme.text_main, 0);
  lv_obj_set_style_text_font(object, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
  lv_obj_align(object, LV_ALIGN_TOP_MID, 0, y);
  return object;
}

void keyboard_open_config(const keyboard_config_t *config,
                          keyboard_submit_cb_t callback,
                          void *context) {
  keyboard_close();
  ui_input_consume_gesture();
  s_config = *config;
  if (s_config.max_chars == 0)
    s_config.max_chars = DEFAULT_LIMIT;
  s_submit = callback;
  s_context = context;
  s_page = s_config.mode == KEYBOARD_NUMBER ? 2 : 0;
  s_screen = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s_screen);
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  lv_obj_set_size(s_screen, ui_screen_w(), ui_screen_h());
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_move_foreground(s_screen);
  s_title = label(s_config.title ? s_config.title : "Edit text", 8);
  lv_obj_set_style_text_color(s_title, current_theme.border_accent, 0);

  s_text = lv_textarea_create(s_screen);
  lv_obj_set_size(s_text, ui_screen_w() - 12, FIELD_H);
  lv_obj_align(s_text, LV_ALIGN_TOP_MID, 0, FIELD_Y);
  lv_textarea_set_one_line(s_text, true);
  lv_textarea_set_max_length(s_text, s_config.max_chars);
  lv_textarea_set_password_mode(s_text, s_config.mode == KEYBOARD_PASSWORD);
  if (s_config.mode == KEYBOARD_HEX)
    lv_textarea_set_accepted_chars(s_text, "0123456789ABCDEFabcdef");
  lv_textarea_set_placeholder_text(s_text,
                                   s_config.mode == KEYBOARD_HEX ? "0-9, A-F" : "Enter text");
  lv_textarea_set_text(s_text, s_config.initial ? s_config.initial : "");
  lv_obj_remove_flag(s_text, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(s_text, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_color(s_text, current_theme.border_accent, 0);
  lv_obj_set_style_text_color(s_text, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_text, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_all(s_text, 8, 0);
  s_count = label("", FIELD_Y + FIELD_H + 5);
  refresh_count();

  s_keys = lv_buttonmatrix_create(s_screen);
  lv_obj_set_size(s_keys, ui_screen_w() - 12, ui_screen_h() - KEYS_Y - FOOTER_H);
  lv_obj_align(s_keys, LV_ALIGN_TOP_MID, 0, KEYS_Y);
  lv_obj_set_style_bg_opa(s_keys, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_keys, 0, 0);
  lv_obj_set_style_pad_all(s_keys, 0, 0);
  lv_obj_set_style_pad_gap(s_keys, 3, 0);
  lv_obj_set_style_text_font(s_keys, &lv_font_montserrat_12, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_keys, current_theme.bg_secondary, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(s_keys, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_text_color(s_keys, current_theme.text_main, LV_PART_ITEMS);
  lv_obj_set_style_border_color(s_keys, current_theme.border_interface, LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_keys, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(s_keys, 4, LV_PART_ITEMS);
  const lv_style_selector_t selected = LV_PART_ITEMS | LV_STATE_FOCUS_KEY;
  lv_obj_set_style_bg_color(s_keys, current_theme.border_accent, selected);
  lv_obj_set_style_text_color(s_keys, current_theme.screen_base, selected);
  lv_obj_set_style_bg_color(s_keys, current_theme.border_accent, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(s_keys, current_theme.screen_base, LV_PART_ITEMS | LV_STATE_PRESSED);
  set_map();
  lv_obj_add_event_cb(s_keys, key_event, LV_EVENT_VALUE_CHANGED, NULL);
  label("OK type   " LV_SYMBOL_OK " confirm   BACK cancel", ui_screen_h() - FOOTER_H + 5);
  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_keys);
    lv_group_set_editing(main_group, true);
    lv_group_focus_obj(s_keys);
  }
}

void keyboard_open(lv_obj_t *target, keyboard_submit_cb_t callback, void *context) {
  const keyboard_config_t config = {.initial = target ? lv_textarea_get_text(target) : NULL};
  keyboard_open_config(&config, callback, context);
}

void keyboard_open_text(const char *initial,
                        uint32_t max_chars,
                        keyboard_submit_cb_t callback,
                        void *context) {
  const keyboard_config_t config = {.initial = initial, .max_chars = max_chars};
  keyboard_open_config(&config, callback, context);
}

bool keyboard_is_open(void) {
  return s_screen != NULL;
}

void keyboard_close(void) {
  if (s_screen == NULL)
    return;
  ui_input_consume_gesture();
  if (main_group != NULL) {
    lv_group_set_editing(main_group, false);
    lv_group_remove_all_objs(main_group);
  }
  lv_obj_delete_async(s_screen);
  s_screen = s_keys = s_text = s_count = s_title = NULL;
  s_submit = NULL;
  s_context = NULL;
}

void keyboard_open_filename(const char *title,
                            const char *initial,
                            uint32_t max_chars,
                            keyboard_submit_cb_t callback) {
  keyboard_open_config(&(keyboard_config_t){.title = title,
                                            .initial = initial,
                                            .max_chars = max_chars,
                                            .min_chars = 1,
                                            .mode = KEYBOARD_FILENAME},
                       callback,
                       NULL);
}

struct keyboard_snapshot {
  size_t bytes;
  keyboard_config_t config;
  keyboard_submit_cb_t callback;
  void *context;
  uint32_t cursor, selected, page;
  char strings[];
};

keyboard_snapshot_t *keyboard_snapshot_capture(void) {
  if (s_screen == NULL)
    return NULL;
  const char *text = lv_textarea_get_text(s_text);
  const char *title = lv_label_get_text(s_title);
  size_t text_len = strlen(text), title_len = strlen(title);
  if (text_len > 4096 || title_len > 128)
    return NULL;
  size_t bytes = sizeof(keyboard_snapshot_t) + text_len + title_len + 2;
  keyboard_snapshot_t *state = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (state == NULL)
    return NULL;
  *state = (keyboard_snapshot_t){.bytes = bytes,
                                 .config = s_config,
                                 .callback = s_submit,
                                 .context = s_context,
                                 .cursor = lv_textarea_get_cursor_pos(s_text),
                                 .selected = lv_buttonmatrix_get_selected_button(s_keys),
                                 .page = s_page};
  memcpy(state->strings, title, title_len + 1);
  memcpy(state->strings + title_len + 1, text, text_len + 1);
  state->config.title = state->strings;
  state->config.initial = state->strings + title_len + 1;
  return state;
}

void keyboard_snapshot_restore(const keyboard_snapshot_t *state) {
  if (state == NULL)
    return;
  keyboard_open_config(&state->config, state->callback, state->context);
  s_page = state->page;
  set_map();
  lv_textarea_set_cursor_pos(s_text, state->cursor);
  uint32_t count = s_config.mode == KEYBOARD_HEX ? HEX_COUNT + 4 : KEY_COUNT + 7;
  lv_buttonmatrix_set_selected_button(s_keys, state->selected < count ? state->selected : 0);
}

void keyboard_snapshot_free(keyboard_snapshot_t *state) {
  if (state == NULL)
    return;
  size_t bytes = state->bytes;
  volatile unsigned char *p = (volatile unsigned char *)state;
  while (bytes--)
    *p++ = 0;
  heap_caps_free(state);
}

void keyboard_mask_preview(void) {
  if (s_text == NULL || s_config.mode != KEYBOARD_PASSWORD)
    return;
  uint32_t time = lv_textarea_get_password_show_time(s_text);
  lv_textarea_set_password_show_time(s_text, 0);
  lv_textarea_set_password_show_time(s_text, time);
}
