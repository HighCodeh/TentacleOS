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

// Apps launcher: lists signed .hb bundles in /sdcard/apps, and launches the
// selected one after a capability-consent modal on first run.

#include "apps_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "lvgl.h"

#include "input_manager.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "tos_api.h"
#include "tos_app_mgr.h"
#include "tos_grants.h"
#include "tos_hb.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define APPS_DIR      "/sdcard/apps"
#define APPS_MAX      20
#define APP_ICON      "/assets/icons/description.bin"
#define APP_MAX_BYTES (512 * 1024)

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static char s_names[APPS_MAX][32]; // base name without ".hb"
static int s_count = 0;

// Pending launch held across the async consent modal.
static uint8_t *s_pending_hb = NULL;
static size_t s_pending_len = 0;
static char s_pending_name[16];
static uint32_t s_pending_caps = 0;

static void caps_str(uint32_t caps, char *out, size_t cap) {
  static const struct {
    uint32_t bit;
    const char *n;
  } kNames[] = {
      {TOS_CAP_FS_READ, "fs-read"},   {TOS_CAP_FS_WRITE, "fs-write"},
      {TOS_CAP_RADIO_TX, "radio-tx"}, {TOS_CAP_RADIO_RX, "radio-rx"},
      {TOS_CAP_HID, "hid"},           {TOS_CAP_UI, "ui"},
      {TOS_CAP_HOSTLINK, "hostlink"}, {TOS_CAP_CONSOLE, "console"},
  };
  out[0] = '\0';
  bool any = false;
  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
    if (caps & kNames[i].bit) {
      if (any)
        strlcat(out, ", ", cap);
      strlcat(out, kNames[i].n, cap);
      any = true;
    }
  }
  if (!any)
    strlcat(out, "(none)", cap);
}

static uint8_t *read_hb(const char *name, size_t *out_len) {
  char path[96];
  snprintf(path, sizeof(path), APPS_DIR "/%s.hb", name);
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > APP_MAX_BYTES) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == NULL) {
    fclose(f);
    return NULL;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (rd != (size_t)sz) {
    heap_caps_free(buf);
    return NULL;
  }
  *out_len = (size_t)sz;
  return buf;
}

static void free_pending(void) {
  if (s_pending_hb != NULL) {
    heap_caps_free(s_pending_hb);
    s_pending_hb = NULL;
  }
  s_pending_len = 0;
}

static void on_consent(bool allow) {
  if (allow && s_pending_hb != NULL) {
    tos_grants_set(s_pending_name, s_pending_caps);
    tos_app_mgr_start(s_pending_hb, s_pending_len, tos_api_get());
  }
  free_pending();
}

static void launch_selected(void) {
  int sel = menu_component_get_selected(&s_menu);
  if (sel < 0 || sel >= s_count)
    return;

  size_t len = 0;
  uint8_t *buf = read_hb(s_names[sel], &len);
  if (buf == NULL) {
    msgbox_open_info(LV_SYMBOL_WARNING, "Error", "Could not read the app", current_theme.border_accent);
    return;
  }

  tos_hb_t meta;
  if (tos_hb_open(buf, len, &meta) != ESP_OK) {
    heap_caps_free(buf);
    msgbox_open_info(LV_SYMBOL_WARNING, "Rejected", "Bad or untrusted signature",
                     current_theme.border_accent);
    return;
  }

  uint32_t missing = meta.caps & ~tos_grants_get(meta.name);
  if (missing == 0) {
    esp_err_t e = tos_app_mgr_start(buf, len, tos_api_get());
    heap_caps_free(buf);
    if (e == ESP_ERR_INVALID_STATE)
      msgbox_open_info(LV_SYMBOL_WARNING, "Full", "Too many apps running", current_theme.border_accent);
    else if (e != ESP_OK)
      msgbox_open_info(LV_SYMBOL_WARNING, "Error", "Could not start the app", current_theme.border_accent);
    else
      msgbox_open_info(LV_SYMBOL_OK, meta.name, "Launched", ui_theme_get_accent());
    return;
  }

  // First run: hold the bundle and ask for consent.
  free_pending();
  s_pending_hb = buf;
  s_pending_len = len;
  strlcpy(s_pending_name, meta.name, sizeof(s_pending_name));
  s_pending_caps = meta.caps;
  char caps[96];
  caps_str(missing, caps, sizeof(caps));
  static char msg[176];
  snprintf(msg, sizeof(msg), "'%s' wants access to:\n%s", meta.name, caps);
  msgbox_open(LV_SYMBOL_WARNING, msg, "Allow", "Deny", on_consent);
}

static void apps_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  bool press = (ev->action == INPUT_ACTION_PRESS);
  bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_OK:
      if (press)
        launch_selected();
      break;
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    default:
      break;
  }
}

static void scan_apps(void) {
  s_count = 0;
  DIR *d = opendir(APPS_DIR);
  if (d == NULL)
    return;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && s_count < APPS_MAX) {
    const char *n = ent->d_name;
    size_t l = strlen(n);
    if (l > 3 && strcmp(n + l - 3, ".hb") == 0) {
      strlcpy(s_names[s_count], n, sizeof(s_names[s_count]));
      s_names[s_count][l - 3] = '\0'; // strip ".hb"
      s_count++;
    }
  }
  closedir(d);
}

void ui_apps_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  free_pending();
  scan_apps();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "APPS", NULL);
  if (s_count == 0) {
    menu_component_add_section(&s_menu, "No apps in /sdcard/apps");
  } else {
    for (int i = 0; i < s_count; i++)
      menu_component_add_item(&s_menu, APP_ICON, s_names[i]);
  }
  menu_component_set_hint(&s_menu, "OK run   BACK exit");

  ui_input_set_screen_handler(apps_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}
