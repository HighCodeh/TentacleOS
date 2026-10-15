// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_cover.h"
#include "misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"

#define COVER_ROOT "/sdcard/games/covers/"
static lv_obj_t *s_owner;
static lv_image_dsc_t s_descriptor;
static uint8_t *s_pixels;

void game_cover_release(void) {
  // Detach the old image before releasing its pixels, including when its
  // outgoing screen is still alive during an asynchronous screen transition.
  if (s_owner != NULL) {
    lv_image_set_src(s_owner, NULL);
    lv_obj_add_flag(s_owner, LV_OBJ_FLAG_HIDDEN);
    s_owner = NULL;
  }
  lv_image_cache_drop(&s_descriptor);
  heap_caps_free(s_pixels);
  s_pixels = NULL;
  memset(&s_descriptor, 0, sizeof(s_descriptor));
}

static void image_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_owner) {
    s_owner = NULL; // Do not operate recursively on the image being deleted.
    game_cover_release();
  }
}

lv_obj_t *game_cover_create(lv_obj_t *parent) {
  lv_obj_t *image = lv_image_create(parent);
  lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
  lv_image_set_antialias(image, false);
  lv_obj_add_event_cb(image, image_deleted, LV_EVENT_DELETE, NULL);
  return image;
}

bool game_cover_show(lv_obj_t *image, const char *key, int box_w, int box_h) {
  game_cover_release();
  if (image == NULL || key == NULL || !key[0] || box_w <= 0 || box_h <= 0 || strchr(key, '/') ||
      strchr(key, '\\') || !strcmp(key, ".") || !strcmp(key, ".."))
    return false;
  char path[304];
  int n = snprintf(path, sizeof(path), COVER_ROOT "%s.hbc", key);
  if (n < 0 || (size_t)n >= sizeof(path))
    return false;
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return false;
  uint8_t header[8];
  bool valid = fread(header, 1, sizeof(header), file) == sizeof(header);
  unsigned w = valid ? (unsigned)header[4] | ((unsigned)header[5] << 8) : 0;
  unsigned h = valid ? (unsigned)header[6] | ((unsigned)header[7] << 8) : 0;
  valid = valid && !memcmp(header, "HBC1", 4) && w > 0 && h > 0 && w <= 160 && h <= 144;
  size_t bytes = w * h * 2;
  if (!valid || fseek(file, 0, SEEK_END) != 0 || ftell(file) != (long)(sizeof(header) + bytes) ||
      fseek(file, sizeof(header), SEEK_SET) != 0) {
    fclose(file);
    return false;
  }
  // Optional decoration must never consume scarce internal RAM.
  uint8_t *pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pixels == NULL) {
    fclose(file);
    return false;
  }
  valid = fread(pixels, 1, bytes, file) == bytes && !ferror(file);
  fclose(file);
  if (!valid) {
    heap_caps_free(pixels);
    return false;
  }
  s_pixels = pixels;
  s_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
  s_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
  s_descriptor.header.w = w;
  s_descriptor.header.h = h;
  s_descriptor.header.stride = w * 2;
  s_descriptor.data_size = bytes;
  s_descriptor.data = pixels;
  s_owner = image;
  lv_image_set_src(image, &s_descriptor);
  int scale_w = box_w * 256 / (int)w;
  int scale_h = box_h * 256 / (int)h;
  int scale = scale_w < scale_h ? scale_w : scale_h;
  lv_image_set_scale(image, scale < 256 ? scale : 256);
  lv_obj_center(image);
  lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
  return true;
}

bool game_cover_show_rom(lv_obj_t *image, const char *rom_path, int box_w, int box_h) {
  char key[256];
  const char *base = rom_path ? strrchr(rom_path, '/') : NULL;
  base = base ? base + 1 : rom_path;
  if (base != NULL && strlen(base) < sizeof(key)) {
    strlcpy(key, base, sizeof(key));
    char *extension = strrchr(key, '.');
    if (extension != NULL)
      *extension = '\0';
    if (game_cover_show(image, key, box_w, box_h))
      return true;
  }
  // Missing per-title artwork must not show another game's title screen.
  game_cover_release();
  return false;
}
