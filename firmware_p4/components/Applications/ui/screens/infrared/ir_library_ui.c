// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ir_library_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "ir_store.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define LIB_ROOT         "/sdcard/ir/library"
#define LIB_BUILTIN_ROOT "/assets/ir"
#define LIB_PAGE_SIZE    16
#define LIB_DIR_MAX      48
#define LIB_DEPTH_MAX    8
#define LIB_PATH_MAX     256
#define LIB_NAME_MAX     96
#define FOLDER_ICON      "/assets/icons/folder_open.bin"
#define REMOTE_ICON      "/assets/icons/settings_remote.bin"

_Static_assert(LIB_PAGE_SIZE + 3 <= MENU_COMP_MAX_ITEMS, "IR page exceeds menu capacity");

typedef struct {
  char name[LIB_NAME_MAX];
  bool directory;
} library_entry_t;

static const char *TAG = "IR_LIBRARY_UI";

EXT_RAM_BSS_ATTR static library_entry_t s_entries[LIB_DIR_MAX];
static lv_obj_t *s_screen;
static menu_component_t s_menu;
static ir_file_t s_remote;
static char s_path[LIB_PATH_MAX];
static char s_remote_name[LIB_NAME_MAX];
static int s_count;
static int s_page;
static int s_depth;
static int s_dir_pages[LIB_DEPTH_MAX];
static int s_dir_selections[LIB_DEPTH_MAX];
static int s_visible;
static int s_previous_row;
static int s_next_row;
static int s_source_row;
static bool s_is_builtin;
static bool s_buttons;
static bool s_missing;
static bool s_limited;

static void build_page(int selected);

static int compare_entries(const void *lhs, const void *rhs) {
  const library_entry_t *a = lhs;
  const library_entry_t *b = rhs;
  if (a->directory != b->directory)
    return a->directory ? -1 : 1;
  return strcasecmp(a->name, b->name);
}

static void scan_directory(void) {
  s_count = 0;
  s_missing = false;
  s_limited = false;
  DIR *dir = opendir(s_path);
  if (dir == NULL) {
    s_missing = true;
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    size_t len = strlen(name);
    if (name[0] == '.' || len == 0)
      continue;
    char path[LIB_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", s_path, name);
    if (len >= LIB_NAME_MAX || n < 0 || (size_t)n >= sizeof(path)) {
      s_limited = true;
      continue;
    }
    struct stat st;
    if (stat(path, &st) != 0)
      continue;
    bool directory = S_ISDIR(st.st_mode);
    if (!directory && (!S_ISREG(st.st_mode) || len < 4 || strcasecmp(name + len - 3, ".ir") != 0))
      continue;
    if (s_count == LIB_DIR_MAX) {
      s_limited = true;
      continue;
    }
    strlcpy(s_entries[s_count].name, name, sizeof(s_entries[s_count].name));
    s_entries[s_count++].directory = directory;
  }
  closedir(dir);
  qsort(s_entries, (size_t)s_count, sizeof(s_entries[0]), compare_entries);
}

static void select_source(bool is_builtin) {
  s_is_builtin = is_builtin;
  s_depth = 0;
  s_page = 0;
  memset(s_dir_pages, 0, sizeof(s_dir_pages));
  memset(s_dir_selections, 0, sizeof(s_dir_selections));
  strlcpy(s_path, is_builtin ? LIB_BUILTIN_ROOT : LIB_ROOT, sizeof(s_path));
  scan_directory();
  ESP_LOGD(TAG, "IR library: %s (%d entries)", s_path, s_count);
}

static int item_count(void) {
  return s_buttons ? (int)s_remote.count : s_count;
}

static void build_page(int selected) {
  if (s_screen == NULL)
    return;
  lv_obj_clean(s_screen);
  int total = item_count();
  int pages = (total + LIB_PAGE_SIZE - 1) / LIB_PAGE_SIZE;
  if (pages < 1)
    pages = 1;
  if (s_page >= pages)
    s_page = pages - 1;
  if (s_page < 0)
    s_page = 0;
  const char *name = s_buttons ? s_remote_name : strrchr(s_path, '/') + 1;
  if (!s_buttons && s_depth == 0)
    name = s_is_builtin ? "Built-in remotes" : "SD library";
  char title[48];
  snprintf(title, sizeof(title), "%.25s %d/%d", name, s_page + 1, pages);
  s_menu = menu_component_create(s_screen, title, REMOTE_ICON);
  s_visible = total - s_page * LIB_PAGE_SIZE;
  if (s_visible > LIB_PAGE_SIZE)
    s_visible = LIB_PAGE_SIZE;
  for (int row = 0; row < s_visible; row++) {
    int index = s_page * LIB_PAGE_SIZE + row;
    char label[LIB_NAME_MAX + 8];
    const char *icon = REMOTE_ICON;
    if (s_buttons) {
      snprintf(label, sizeof(label), "%02d  %s", index + 1, s_remote.signals[index].name);
    } else {
      strlcpy(label, s_entries[index].name, sizeof(label));
      if (s_entries[index].directory)
        icon = FOLDER_ICON;
      else
        label[strlen(label) - 3] = '\0';
    }
    menu_component_add_item(&s_menu, icon, label);
  }
  if (s_visible == 0)
    menu_component_add_item(
        &s_menu, FOLDER_ICON, s_missing ? "Library unavailable" : "No remotes here");
  s_source_row = -1;
  if (!s_buttons && s_depth == 0) {
    s_source_row = s_menu.item_count;
    menu_component_add_item(
        &s_menu, FOLDER_ICON, s_is_builtin ? "Browse SD library" : "Browse built-in remotes");
  }
  s_previous_row = -1;
  s_next_row = -1;
  if (s_page > 0) {
    s_previous_row = s_menu.item_count;
    menu_component_add_item(&s_menu, NULL, "Previous page");
  }
  if (s_page + 1 < pages) {
    s_next_row = s_menu.item_count;
    menu_component_add_item(&s_menu, NULL, "Next page");
  }
  menu_component_set_hint(&s_menu,
                          s_buttons ? "OK send  L/R page  BACK up" : "OK open  L/R page  BACK up");
  menu_component_select(&s_menu, selected < s_menu.item_count ? selected : 0);
}

static void open_selected(int row) {
  if (row < 0 || row >= s_visible)
    return;
  int index = s_page * LIB_PAGE_SIZE + row;
  if (s_buttons) {
    const ir_signal_t *signal = &s_remote.signals[index];
    esp_err_t err = ir_store_send_signal(signal);
    char message[80];
    if (err == ESP_OK)
      snprintf(message, sizeof(message), "IR sent: %.31s", signal->name);
    else
      snprintf(message, sizeof(message), "IR send failed: %s", esp_err_to_name(err));
    notify(err == ESP_OK ? NOTIFY_INFO : NOTIFY_WARNING, message);
    return;
  }
  library_entry_t *entry = &s_entries[index];
  char path[LIB_PATH_MAX];
  int n = snprintf(path, sizeof(path), "%s/%s", s_path, entry->name);
  if (n < 0 || (size_t)n >= sizeof(path)) {
    notify(NOTIFY_WARNING, "Remote path is too long");
    return;
  }
  s_dir_pages[s_depth] = s_page;
  s_dir_selections[s_depth] = row;
  if (entry->directory) {
    if (s_depth + 1 >= LIB_DEPTH_MAX) {
      notify(NOTIFY_WARNING, "Folder depth limit reached");
      return;
    }
    strlcpy(s_path, path, sizeof(s_path));
    s_depth++;
    s_page = 0;
    scan_directory();
    build_page(0);
    if (s_limited)
      notify(NOTIFY_WARNING, "Folder limit: 48 short names");
    return;
  }
  ir_file_free(&s_remote);
  ir_file_init(&s_remote);
  esp_err_t err = ir_store_load(path, &s_remote);
  if (err != ESP_OK || s_remote.count == 0) {
    ir_file_free(&s_remote);
    char message[80];
    if (err == ESP_OK)
      strlcpy(message, "No supported IR signals", sizeof(message));
    else
      snprintf(message, sizeof(message), "Cannot read remote: %s", esp_err_to_name(err));
    notify(NOTIFY_WARNING, message);
    return;
  }
  strlcpy(s_remote_name, entry->name, sizeof(s_remote_name));
  s_remote_name[strlen(s_remote_name) - 3] = '\0';
  s_buttons = true;
  s_page = 0;
  build_page(0);
}

static void go_back(void) {
  if (s_buttons) {
    ir_file_free(&s_remote);
    s_buttons = false;
  } else if (s_depth > 0) {
    char *slash = strrchr(s_path, '/');
    if (slash != NULL)
      *slash = '\0';
    s_depth--;
  } else {
    ui_switch_screen(SCREEN_IR_MENU);
    return;
  }
  s_page = s_dir_pages[s_depth];
  scan_directory();
  build_page(s_dir_selections[s_depth]);
}

static void library_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (s_screen == NULL || lv_screen_active() != s_screen)
    return;
  bool press = ev->action == INPUT_ACTION_PRESS;
  bool nav = press || ev->action == INPUT_ACTION_REPEAT;
  if (ev->button == INPUT_BTN_DOWN && nav)
    menu_component_next(&s_menu);
  else if (ev->button == INPUT_BTN_UP && nav)
    menu_component_prev(&s_menu);
  else if (ev->button == INPUT_BTN_BACK && press)
    go_back();
  else if (ev->button == INPUT_BTN_LEFT && press && s_page > 0) {
    s_page--;
    build_page(0);
  } else if (ev->button == INPUT_BTN_RIGHT && press &&
             (s_page + 1) * LIB_PAGE_SIZE < item_count()) {
    s_page++;
    build_page(0);
  } else if (ev->button == INPUT_BTN_OK && press) {
    int selected = menu_component_get_selected(&s_menu);
    if (selected == s_source_row) {
      select_source(!s_is_builtin);
      build_page(0);
    } else if (selected == s_previous_row) {
      s_page--;
      build_page(0);
    } else if (selected == s_next_row) {
      s_page++;
      build_page(0);
    } else if (s_visible == 0) {
      scan_directory();
      build_page(0);
    } else {
      open_selected(selected);
    }
  }
}

static void screen_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  ir_file_free(&s_remote);
  s_buttons = false;
}

void ui_ir_library_open(void) {
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  ir_file_init(&s_remote);
  s_buttons = false;
  select_source(false);
  if (s_missing || s_count == 0)
    select_source(true);
  lv_obj_t *outgoing = lv_screen_active();
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_screen, screen_deleted, LV_EVENT_DELETE, NULL);
  build_page(0);
  ui_input_set_screen_handler(library_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  if (outgoing != NULL && outgoing != s_screen && lv_obj_is_valid(outgoing))
    lv_obj_delete_async(outgoing);
  if (s_limited)
    notify(NOTIFY_WARNING, "Folder limit: 48 short names");
}
