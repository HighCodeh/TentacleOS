// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.



#include "ducky_parser.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "class/hid/hid_device.h"
#include "storage_assets.h"
#include "storage_read.h"
#include "storage_impl.h"
#include "hid_hal.h"
#include "hid_layouts.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "DUCKY_PARSER";
static volatile bool s_abort_flag = false;
static ducky_progress_cb_t s_progress_cb = NULL;
static ducky_layout_t s_layout = DUCKY_LAYOUT_US;

void ducky_set_progress_callback(ducky_progress_cb_t cb) { s_progress_cb = cb; }
void ducky_set_layout(ducky_layout_t layout) {
  s_layout = layout;
  ESP_LOGI(TAG, "Keyboard layout set to: %s", layout == DUCKY_LAYOUT_ABNT2 ? "ABNT2" : "US");
}

typedef struct { const char* name; uint8_t code; } key_map_t;
static const key_map_t key_map[] = {
  {"ENTER", HID_KEY_ENTER}, {"RETURN", HID_KEY_ENTER}, {"ESC", HID_KEY_ESCAPE},
  {"ESCAPE", HID_KEY_ESCAPE}, {"BACKSPACE", HID_KEY_BACKSPACE}, {"TAB", HID_KEY_TAB},
  {"SPACE", HID_KEY_SPACE}, {"CAPSLOCK", HID_KEY_CAPS_LOCK}, {"PRINTSCREEN", HID_KEY_PRINT_SCREEN},
  {"SCROLLLOCK", HID_KEY_SCROLL_LOCK}, {"PAUSE", HID_KEY_PAUSE}, {"INSERT", HID_KEY_INSERT},
  {"HOME", HID_KEY_HOME}, {"PAGEUP", HID_KEY_PAGE_UP}, {"DELETE", HID_KEY_DELETE},
  {"END", HID_KEY_END}, {"PAGEDOWN", HID_KEY_PAGE_DOWN}, {"RIGHT", HID_KEY_ARROW_RIGHT},
  {"RIGHTARROW", HID_KEY_ARROW_RIGHT}, {"LEFT", HID_KEY_ARROW_LEFT}, {"LEFTARROW", HID_KEY_ARROW_LEFT},
  {"DOWN", HID_KEY_ARROW_DOWN}, {"DOWNARROW", HID_KEY_ARROW_DOWN}, {"UP", HID_KEY_ARROW_UP},
  {"UPARROW", HID_KEY_ARROW_UP}, {"NUMLOCK", HID_KEY_NUM_LOCK}, {"APP", HID_KEY_APPLICATION},
  {"MENU", HID_KEY_APPLICATION}, {"F1", HID_KEY_F1}, {"F2", HID_KEY_F2}, {"F3", HID_KEY_F3},
  {"F4", HID_KEY_F4}, {"F5", HID_KEY_F5}, {"F6", HID_KEY_F6}, {"F7", HID_KEY_F7},
  {"F8", HID_KEY_F8}, {"F9", HID_KEY_F9}, {"F10", HID_KEY_F10}, {"F11", HID_KEY_F11},
  {"F12", HID_KEY_F12}, {NULL, 0}
};

static uint8_t find_key_code(const char* str) {
  if (strlen(str) == 1) {
    char c = toupper((unsigned char)str[0]);
    if (c >= 'A' && c <= 'Z') return HID_KEY_A + (c - 'A');
    if (c >= '1' && c <= '9') return HID_KEY_1 + (c - '1');
    if (c == '0') return HID_KEY_0;
  }
  for (int i = 0; key_map[i].name != NULL; i++) {
    if (strcasecmp(key_map[i].name, str) == 0) return key_map[i].code;
  }
  return 0;
}

static void trim_newline(char* str) {
  size_t len = strlen(str);
  while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n')) {
    str[len - 1] = '\0';
    len--;
  }
}

static bool is_modifier(const char* word, uint8_t* current_mod) {
  if (strcasecmp(word, "CTRL") == 0 || strcasecmp(word, "CONTROL") == 0) { *current_mod |= KEYBOARD_MODIFIER_LEFTCTRL; return true; }
  if (strcasecmp(word, "SHIFT") == 0) { *current_mod |= KEYBOARD_MODIFIER_LEFTSHIFT; return true; }
  if (strcasecmp(word, "ALT") == 0) { *current_mod |= KEYBOARD_MODIFIER_LEFTALT; return true; }
  if (strcasecmp(word, "GUI") == 0 || strcasecmp(word, "WINDOWS") == 0 || strcasecmp(word, "COMMAND") == 0) { *current_mod |= KEYBOARD_MODIFIER_LEFTGUI; return true; }
  return false;
}

static void process_line(char* line) {
  if (strlen(line) < 2 || strncmp(line, "REM", 3) == 0) return;
  char *saveptr = NULL;
  char* cmd = strtok_r(line, " ", &saveptr);
  if (!cmd) return;

  if (strcmp(cmd, "DELAY") == 0) {
    char* arg = strtok_r(NULL, " ", &saveptr);
    if (arg) { int ms = atoi(arg); if (ms > 0) vTaskDelay(pdMS_TO_TICKS(ms)); }
  } else if (strcmp(cmd, "STRING") == 0) {
    char* next_token = strtok_r(NULL, "", &saveptr);
    if (next_token) {
      if (s_layout == DUCKY_LAYOUT_ABNT2) type_string_abnt2(next_token);
      else type_string_us(next_token);
    }
  } else {
    uint8_t modifiers = 0; uint8_t keycode = 0;
    if (is_modifier(cmd, &modifiers)) {
      char* token;
      while ((token = strtok_r(NULL, " ", &saveptr)) != NULL) {
        if (!is_modifier(token, &modifiers)) keycode = find_key_code(token);
      }
    } else { keycode = find_key_code(cmd); }
    if (keycode != 0 || modifiers != 0) hid_hal_press_key(keycode, modifiers);
  }
}

void ducky_abort(void) { s_abort_flag = true; }
void ducky_parse_and_run(const char *script) {
  if (!script) return;
  s_abort_flag = false;
  char* script_copy = strdup(script);
  if (!script_copy) return;
  int total_lines = 0; const char *p = script;
  while (*p) { if (*p == '\n') total_lines++; p++; }
  if (p > script && *(p-1) != '\n') total_lines++; 
  int current_line = 0; char *saveptr = NULL;
  char* line = strtok_r(script_copy, "\n", &saveptr);
  while (line != NULL) {
    if (s_abort_flag) break;
    current_line++;
    if (s_progress_cb) s_progress_cb(current_line, total_lines);
    trim_newline(line);
    process_line(line);
    vTaskDelay(pdMS_TO_TICKS(20)); 
    line = strtok_r(NULL, "\n", &saveptr);
  }
  if (s_progress_cb) s_progress_cb(total_lines, total_lines);
  free(script_copy);
}

esp_err_t ducky_run_from_assets(const char *filename) {
  size_t size = 0;
  char *buffer = (char *)storage_assets_load_file(filename, &size);
  if (!buffer) return ESP_ERR_NOT_FOUND;
  char *script_str = malloc(size + 1);
  if (!script_str) { free(buffer); return ESP_ERR_NO_MEM; }
  memcpy(script_str, buffer, size);
  script_str[size] = '\0';
  free(buffer);
  ducky_parse_and_run(script_str);
  free(script_str);
  return ESP_OK;
}
