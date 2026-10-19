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

#include "scripts_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "lvgl.h"

#include "menu_component_ui.h"
#include "notify_ui.h"
#include "script_runner.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define SCRIPT_LIST_MAX   16
#define SCRIPT_REFRESH_MS 100
#define SCRIPT_ICON       "/assets/icons/developer_board.bin"
#define SCRIPT_PATH_MAX   (sizeof(SCRIPT_RUNNER_DIRECTORY) + SCRIPT_RUNNER_NAME_MAX)

_Static_assert(SCRIPT_LIST_MAX <= MENU_COMP_MAX_ITEMS, "Script list exceeds menu capacity");

static const char *TAG = "SCRIPTS_UI";

static lv_obj_t *s_screen;
static lv_obj_t *s_output;
static lv_obj_t *s_status;
static lv_timer_t *s_timer;
static menu_component_t s_menu;
static char s_files[SCRIPT_LIST_MAX][SCRIPT_RUNNER_NAME_MAX];
static size_t s_count;
static bool s_is_output_view;
EXT_RAM_BSS_ATTR static script_runner_snapshot_t s_snapshot;

static int compare_names(const void *left, const void *right) {
  return strcmp(left, right);
}

static void build_list(void) {
  s_is_output_view = false;
  s_output = NULL;
  s_status = NULL;
  lv_obj_clean(s_screen);
  s_menu = menu_component_create(s_screen, "SCRIPTS / JS + LUA", SCRIPT_ICON);
  s_count = 0;
  DIR *directory = opendir(SCRIPT_RUNNER_DIRECTORY);
  if (directory != NULL) {
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && s_count < SCRIPT_LIST_MAX) {
      if (!script_runner_name_is_valid(entry->d_name))
        continue;
      char path[SCRIPT_PATH_MAX];
      snprintf(path, sizeof(path), "%s/%s", SCRIPT_RUNNER_DIRECTORY, entry->d_name);
      struct stat info;
      if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
        continue;
      snprintf(s_files[s_count], sizeof(s_files[s_count]), "%s", entry->d_name);
      s_count++;
    }
    closedir(directory);
  }
  qsort(s_files, s_count, sizeof(s_files[0]), compare_names);
  for (size_t i = 0; i < s_count; ++i)
    menu_component_add_item(&s_menu, SCRIPT_ICON, s_files[i]);
  if (s_count == 0)
    menu_component_add_item(&s_menu, SCRIPT_ICON, "No .js / .lua in /sdcard/scripts");
  menu_component_set_hint(&s_menu, "OK run/refresh   BACK developer");
}

static void refresh_output(lv_timer_t *timer) {
  (void)timer;
  if (!s_is_output_view || s_output == NULL)
    return;
  script_runner_snapshot(&s_snapshot);
  lv_label_set_text(s_output, s_snapshot.output);
  if (s_snapshot.is_running)
    lv_label_set_text(s_status, "Running  |  OK stop  BACK list");
  else if (s_snapshot.result == SCRIPT_ENGINE_OK)
    lv_label_set_text(s_status, "Finished  |  OK/BACK list");
  else if (s_snapshot.result == SCRIPT_ENGINE_STOPPED)
    lv_label_set_text(s_status, "Stopped / time limit  |  OK/BACK list");
  else
    lv_label_set_text_fmt(s_status,
                          "Failed: %s",
                          s_snapshot.error[0] == '\0' ? "execution or memory error"
                                                      : s_snapshot.error);
}

static void show_output(void) {
  s_is_output_view = true;
  lv_obj_clean(s_screen);
  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(panel, current_theme.screen_base, 0);
  s_status = lv_label_create(panel);
  lv_obj_set_width(s_status, lv_pct(100));
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  s_output = lv_label_create(panel);
  lv_obj_set_width(s_output, lv_pct(100));
  lv_label_set_long_mode(s_output, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(s_output, current_theme.text_main, 0);
  refresh_output(NULL);
}

static void scripts_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool is_navigation = is_press || event->action == INPUT_ACTION_REPEAT;
  if (s_is_output_view) {
    if (is_press && event->button == INPUT_BTN_BACK) {
      script_runner_stop();
      build_list();
    } else if (is_press && event->button == INPUT_BTN_OK) {
      script_runner_snapshot(&s_snapshot);
      if (s_snapshot.is_running)
        script_runner_stop();
      else
        build_list();
    }
    return;
  }
  if (is_navigation && event->button == INPUT_BTN_DOWN)
    menu_component_next(&s_menu);
  else if (is_navigation && event->button == INPUT_BTN_UP)
    menu_component_prev(&s_menu);
  else if (is_press && event->button == INPUT_BTN_BACK)
    ui_switch_screen(SCREEN_DEV_MENU);
  else if (is_press && event->button == INPUT_BTN_OK) {
    int selected = menu_component_get_selected(&s_menu);
    if (s_count == 0) {
      build_list();
      return;
    }
    if (selected < 0 || (size_t)selected >= s_count)
      return;
    esp_err_t error = script_runner_start(s_files[selected]);
    if (error != ESP_OK) {
      ESP_LOGW(TAG, "Script start failed: %s", esp_err_to_name(error));
      notify(NOTIFY_WARNING, "Script busy or unable to start");
      return;
    }
    show_output();
  }
}

static void screen_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  ui_scripts_stop();
  if (s_timer != NULL) {
    lv_timer_del(s_timer);
    s_timer = NULL;
  }
  s_output = NULL;
  s_status = NULL;
  s_screen = NULL;
}

void ui_scripts_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_screen, screen_deleted, LV_EVENT_DELETE, NULL);
  build_list();
  s_timer = lv_timer_create(refresh_output, SCRIPT_REFRESH_MS, NULL);
  ui_input_set_screen_handler(scripts_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

void ui_scripts_stop(void) {
  script_runner_stop();
}
