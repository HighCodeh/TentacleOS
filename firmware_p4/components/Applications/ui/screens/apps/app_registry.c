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

#include "app_registry.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "msgbox_ui.h"
#include "tos_api.h"
#include "tos_app_mgr.h"
#include "tos_grants.h"
#include "tos_hb.h"
#include "ui_theme.h"

#define APPS_DIR        "/sdcard/apps"
#define APP_MAX_BYTES   (512 * 1024)
#define APP_META_MAX    (32 * 1024) // defensive cap on an unverified meta block
#define APP_ICON_MAX_WH 64          // reject spoofed oversized icons at scan

static void title_cstr(const char *title, uint16_t title_len, char *out, size_t cap) {
  size_t n = title_len;
  if (n > cap - 1)
    n = cap - 1;
  if (title != NULL && n > 0)
    memcpy(out, title, n);
  else
    n = 0;
  out[n] = '\0';
}

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
}

// Decode a .hb meta icon record (bin_header_t + pixels) into an image dsc with a
// caller-owned pixel copy. Bounds-checked: the source is an UNVERIFIED scan-time
// read, so anything malformed returns false (no icon).
static bool
build_icon_dsc(const uint8_t *icon, uint32_t icon_len, lv_image_dsc_t *dsc, uint8_t **out_pixels) {
  *out_pixels = NULL;
  if (icon == NULL || icon_len < 8)
    return false;
  uint32_t magic_cf = (uint32_t)icon[0] | ((uint32_t)icon[1] << 8) | ((uint32_t)icon[2] << 16) |
                      ((uint32_t)icon[3] << 24);
  uint16_t w = (uint16_t)(icon[4] | (icon[5] << 8));
  uint16_t h = (uint16_t)(icon[6] | (icon[7] << 8));
  if (w == 0 || h == 0 || w > APP_ICON_MAX_WH || h > APP_ICON_MAX_WH)
    return false;

  lv_color_format_t cf = LV_COLOR_FORMAT_ARGB8888;
  if ((magic_cf & 0xFF) == LV_IMAGE_HEADER_MAGIC)
    cf = (lv_color_format_t)((magic_cf >> 8) & 0xFF);
  uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
  uint32_t data_size = stride * h;
  if (cf == LV_COLOR_FORMAT_RGB565A8)
    data_size += (stride / 2) * h;
  if ((uint32_t)(icon_len - 8) < data_size)
    return false;

  uint8_t *px = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (px == NULL)
    return false;
  memcpy(px, icon + 8, data_size);

  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = cf;
  dsc->header.w = w;
  dsc->header.h = h;
  dsc->header.stride = stride;
  dsc->data = px;
  dsc->data_size = data_size;
  *out_pixels = px;
  return true;
}

static void peek_entry(app_reg_entry_t *e) {
  char path[96];
  snprintf(path, sizeof(path), APPS_DIR "/%s.hb", e->file);
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return;

  uint8_t hdr[TOS_HB_HDR_SIZE];
  if (fread(hdr, 1, TOS_HB_HDR_SIZE, f) != TOS_HB_HDR_SIZE) {
    fclose(f);
    return;
  }
  tos_hb_t h;
  uint32_t elf_len = 0, meta_len = 0;
  if (tos_hb_read_header(hdr, &h, &elf_len, &meta_len) != ESP_OK || meta_len == 0 ||
      meta_len > APP_META_MAX) {
    fclose(f);
    return;
  }
  if (fseek(f, (long)((uint32_t)TOS_HB_HDR_SIZE + elf_len), SEEK_SET) != 0) {
    fclose(f);
    return;
  }
  uint8_t *meta = heap_caps_malloc(meta_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (meta == NULL) {
    fclose(f);
    return;
  }
  size_t rd = fread(meta, 1, meta_len, f);
  fclose(f);
  if (rd != meta_len) {
    heap_caps_free(meta);
    return;
  }

  tos_hb_t m;
  tos_hb_parse_meta(meta, meta_len, &m);
  if (m.title != NULL && m.title_len > 0)
    title_cstr(m.title, m.title_len, e->title, sizeof(e->title));
  if (m.icon != NULL && m.icon_len > 0)
    e->has_icon = build_icon_dsc(m.icon, m.icon_len, &e->icon, &e->icon_px);

  heap_caps_free(meta);
}

int app_registry_scan(app_reg_entry_t *out, int max) {
  int n = 0;
  DIR *d = opendir(APPS_DIR);
  if (d == NULL)
    return 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && n < max) {
    const char *name = ent->d_name;
    size_t l = strlen(name);
    if (l > 3 && strcmp(name + l - 3, ".hb") == 0) {
      app_reg_entry_t *e = &out[n];
      memset(e, 0, sizeof(*e));
      strlcpy(e->file, name, sizeof(e->file));
      e->file[l - 3] = '\0'; // strip ".hb"
      peek_entry(e);
      n++;
    }
  }
  closedir(d);
  return n;
}

void app_registry_free(app_reg_entry_t *list, int count) {
  for (int i = 0; i < count; i++) {
    if (list[i].icon_px != NULL) {
      heap_caps_free(list[i].icon_px);
      list[i].icon_px = NULL;
    }
    list[i].has_icon = false;
  }
}

// --- launch (read + verify + consent + start) ---------------------------------

static uint8_t *s_pending_hb = NULL;
static size_t s_pending_len = 0;
static char s_pending_name[16];
static uint32_t s_pending_caps = 0;

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

void app_registry_launch(const char *file) {
  if (file == NULL || file[0] == '\0')
    return;

  size_t len = 0;
  uint8_t *buf = read_hb(file, &len);
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

  char disp[64];
  if (meta.title != NULL && meta.title_len > 0)
    title_cstr(meta.title, meta.title_len, disp, sizeof(disp));
  else
    strlcpy(disp, meta.name, sizeof(disp));

  uint32_t missing = meta.caps & ~tos_grants_get(meta.name);
  if (missing == 0) {
    esp_err_t e = tos_app_mgr_start(buf, len, tos_api_get());
    heap_caps_free(buf);
    if (e == ESP_ERR_INVALID_STATE)
      msgbox_open_info(LV_SYMBOL_WARNING, "Full", "Too many apps running", current_theme.border_accent);
    else if (e == ESP_ERR_INVALID_VERSION)
      msgbox_open_info(LV_SYMBOL_WARNING, "Incompatible", "App needs a newer firmware",
                       current_theme.border_accent);
    else if (e != ESP_OK)
      msgbox_open_info(LV_SYMBOL_WARNING, "Error", "Could not start the app", current_theme.border_accent);
    else if (!(meta.caps & TOS_CAP_UI))
      msgbox_open_info(LV_SYMBOL_OK, disp, "Launched", ui_theme_get_accent());
    return;
  }

  free_pending();
  s_pending_hb = buf;
  s_pending_len = len;
  strlcpy(s_pending_name, meta.name, sizeof(s_pending_name)); // identity, for grants
  s_pending_caps = meta.caps;
  char caps[96];
  caps_str(missing, caps, sizeof(caps));
  static char msg[176];
  snprintf(msg, sizeof(msg), "'%s' wants access to:\n%s", disp, caps);
  msgbox_open(LV_SYMBOL_WARNING, msg, "Allow", "Deny", on_consent);
}
