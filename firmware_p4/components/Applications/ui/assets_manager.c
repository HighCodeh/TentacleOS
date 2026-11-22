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

#include "assets_manager.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"

#include "lvgl_glue.h"

#include "draw/lv_image_decoder_private.h"
#include "libs/lz4/lz4.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "misc/cache/lv_cache_private.h"

static const char *TAG = "ASSETS_MANAGER";

#define ASSETS_BIN_FLAGS_SHIFT 16
#define ASSETS_BIN_MAGIC_MASK  0xFF

typedef struct {
  uint32_t method;
  uint32_t compressed_size;
  uint32_t decompressed_size;
} assets_compressed_header_t;

typedef struct __attribute__((packed)) {
  uint32_t magic_cf;
  uint16_t w;
  uint16_t h;
  uint32_t stride;
} bin_header_t;

typedef struct asset_node {
  lv_image_dsc_t dsc;
  char *path;
  char *file;
  uint8_t *sd_data;
  struct asset_node *next;
} asset_node_t;

#define ASSETS_OVERRIDE_MAX  4
#define ASSETS_PREFIX_MAX    32
#define ASSETS_SDDIR_MAX     96
#define ASSETS_FILE_MAX      160
#define ASSETS_FILELIST_INIT 16

typedef struct {
  char flash_prefix[ASSETS_PREFIX_MAX];
  char sd_dir[ASSETS_SDDIR_MAX];
  char **files;
  int file_count;
} assets_override_t;

static assets_override_t s_overrides[ASSETS_OVERRIDE_MAX];
static int s_override_count = 0;

static asset_node_t *s_assets_head = NULL;
static bool s_decoder_registered = false;

// Linear scan, but move a hit to the front so the assets a screen re-requests
// every build (header icons, etc.) settle near the head. Relinking only reorders
// the list; the nodes do not move, so every &node->dsc handed out stays valid.
static asset_node_t *find_node_by_path(const char *path) {
  asset_node_t *prev = NULL;
  for (asset_node_t *n = s_assets_head; n; prev = n, n = n->next) {
    if (strcmp(n->path, path) == 0) {
      if (prev != NULL) {
        prev->next = n->next;
        n->next = s_assets_head;
        s_assets_head = n;
      }
      return n;
    }
  }
  return NULL;
}

static asset_node_t *find_node_by_dsc(const void *src) {
  for (asset_node_t *n = s_assets_head; n; n = n->next) {
    if ((const void *)&n->dsc == src)
      return n;
  }
  return NULL;
}

static bool read_bin_header(const char *path, bin_header_t *out) {
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return false;
  bool ok = fread(out, 1, sizeof(*out), f) == sizeof(*out);
  ok = fclose(f) == 0 && ok;
  ok = ok && (out->magic_cf & ASSETS_BIN_MAGIC_MASK) == LV_IMAGE_HEADER_MAGIC && out->w != 0 &&
       out->h != 0;
  return ok;
}

static bool override_has(const assets_override_t *ov, const char *name) {
  for (int j = 0; j < ov->file_count; j++) {
    if (strcmp(ov->files[j], name) == 0)
      return true;
  }
  return false;
}

static void resolve_file(const char *path, char *out, size_t out_size) {
  for (int i = 0; i < s_override_count; i++) {
    size_t plen = strlen(s_overrides[i].flash_prefix);
    if (strncmp(path, s_overrides[i].flash_prefix, plen) != 0)
      continue;
    const char *tail = path + plen;
    if (*tail == '/')
      tail++;
    if (!override_has(&s_overrides[i], tail))
      continue;
    int n = snprintf(out, out_size, "%s/%s", s_overrides[i].sd_dir, tail);
    if (n > 0 && (size_t)n < out_size)
      return;
  }
  strlcpy(out, path, out_size);
}

static void header_to_dsc(const bin_header_t *hdr, lv_image_dsc_t *dsc) {
  lv_color_format_t cf = LV_COLOR_FORMAT_ARGB8888;
  if ((hdr->magic_cf & 0xFF) == LV_IMAGE_HEADER_MAGIC)
    cf = (lv_color_format_t)((hdr->magic_cf >> 8) & 0xFF);
  uint32_t stride = lv_draw_buf_width_to_stride(hdr->w, cf);
  uint32_t data_size = stride * hdr->h;
  if (cf == LV_COLOR_FORMAT_RGB565A8)
    data_size += (stride / 2) * hdr->h;
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = cf;
  dsc->header.w = hdr->w;
  dsc->header.h = hdr->h;
  dsc->header.stride = stride;
  dsc->header.flags = 0;
  dsc->data_size = data_size;
}

static bool read_pixels(const char *path, uint8_t *out_pixels, size_t size) {
  bool is_valid = false;
  uint8_t *compressed = NULL;
  FILE *file = fopen(path, "rb");
  bin_header_t header;
  assets_compressed_header_t packed;
  lv_image_dsc_t expected = {0};
  if (file == NULL || size == 0 || size > INT_MAX ||
      fread(&header, 1, sizeof(header), file) != sizeof(header) ||
      (header.magic_cf & ASSETS_BIN_MAGIC_MASK) != LV_IMAGE_HEADER_MAGIC)
    goto cleanup;
  header_to_dsc(&header, &expected);
  if (expected.data_size != size)
    goto cleanup;

  uint16_t flags = header.magic_cf >> ASSETS_BIN_FLAGS_SHIFT;
  if ((flags & LV_IMAGE_FLAGS_COMPRESSED) == 0) {
    is_valid = fread(out_pixels, 1, size, file) == size;
    goto cleanup;
  }
  if (flags != LV_IMAGE_FLAGS_COMPRESSED ||
      fread(&packed, 1, sizeof(packed), file) != sizeof(packed) ||
      packed.method != LV_IMAGE_COMPRESS_LZ4 || packed.decompressed_size != size ||
      packed.compressed_size == 0 || packed.compressed_size > size)
    goto cleanup;

  compressed = heap_caps_malloc(packed.compressed_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (compressed == NULL) {
    ESP_LOGE(TAG, "Compressed buffer allocation failed for %s", path);
    goto cleanup;
  }
  if (fread(compressed, 1, packed.compressed_size, file) != packed.compressed_size ||
      fgetc(file) != EOF || ferror(file))
    goto cleanup;
  is_valid = LZ4_decompress_safe((const char *)compressed,
                                 (char *)out_pixels,
                                 (int)packed.compressed_size,
                                 (int)size) == (int)size;

cleanup:
  free(compressed);
  if (file != NULL && fclose(file) != 0)
    is_valid = false;
  if (!is_valid)
    ESP_LOGE(TAG, "Invalid or unreadable pixels: %s", path);
  return is_valid;
}

static void load_sd_pixels(asset_node_t *n) {
  free(n->sd_data);
  n->sd_data = NULL;
  if (n->file == NULL || strncmp(n->file, "/sdcard", 7) != 0)
    return;
  size_t sz = n->dsc.data_size;
  if (sz == 0)
    return;
  uint8_t *buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == NULL) {
    ESP_LOGW(TAG, "sd preload alloc failed for %s (%u B)", n->file, (unsigned)sz);
    return;
  }
  if (!read_pixels(n->file, buf, sz)) {
    free(buf);
    return;
  }
  n->sd_data = buf;
}

static void refresh_overridden_nodes(void) {
  for (asset_node_t *n = s_assets_head; n != NULL; n = n->next) {
    char eff[ASSETS_FILE_MAX];
    resolve_file(n->path, eff, sizeof(eff));
    if (n->file != NULL && strcmp(eff, n->file) == 0)
      continue;
    bin_header_t hdr;
    if (!read_bin_header(eff, &hdr))
      continue;
    char *nf = strdup(eff);
    if (nf == NULL)
      continue;
    header_to_dsc(&hdr, &n->dsc);
    free(n->file);
    n->file = nf;
    n->dsc.data = (const uint8_t *)n->file;
    load_sd_pixels(n);
    lv_image_cache_drop(&n->dsc);
  }
}

static lv_result_t asset_decoder_info(lv_image_decoder_t *decoder,
                                      lv_image_decoder_dsc_t *dsc,
                                      lv_image_header_t *header) {
  (void)decoder;
  if (dsc->src_type != LV_IMAGE_SRC_VARIABLE)
    return LV_RESULT_INVALID;
  asset_node_t *node = find_node_by_dsc(dsc->src);
  if (node == NULL)
    return LV_RESULT_INVALID;
  *header = node->dsc.header;
  return LV_RESULT_OK;
}

static lv_result_t asset_decoder_open(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc) {
  asset_node_t *node = find_node_by_dsc(dsc->src);
  if (node == NULL)
    return LV_RESULT_INVALID;

  const uint32_t w = node->dsc.header.w;
  const uint32_t h = node->dsc.header.h;

  lv_draw_buf_t *buf = lv_draw_buf_create(w, h, node->dsc.header.cf, LV_STRIDE_AUTO);
  if (buf == NULL) {
    ESP_LOGE(TAG,
             "draw buf alloc failed for %s (%lux%lu)",
             node->file,
             (unsigned long)w,
             (unsigned long)h);
    return LV_RESULT_INVALID;
  }

  if (node->sd_data != NULL) {
    memcpy(buf->data, node->sd_data, buf->data_size);
  } else if (!read_pixels(node->file, buf->data, buf->data_size)) {
    lv_draw_buf_destroy(buf);
    return LV_RESULT_INVALID;
  }

  dsc->decoded = buf;

  if (lv_image_cache_is_enabled()) {
    lv_image_cache_data_t key;
    key.src_type = dsc->src_type;
    key.src = dsc->src;
    key.slot.size = buf->data_size;
    lv_cache_entry_t *entry = lv_image_decoder_add_to_cache(decoder, &key, buf, NULL);
    if (entry == NULL) {
      lv_draw_buf_destroy(buf);
      dsc->decoded = NULL;
      return LV_RESULT_INVALID;
    }
    dsc->cache_entry = entry;
    dsc->user_data = NULL;
  } else {
    dsc->user_data = buf;
  }

  return LV_RESULT_OK;
}

static void asset_decoder_close(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc) {
  (void)decoder;
  if (dsc->user_data) {
    lv_draw_buf_destroy((lv_draw_buf_t *)dsc->user_data);
    dsc->user_data = NULL;
  }
}

static void register_decoder(void) {
  if (s_decoder_registered)
    return;
  lv_image_decoder_t *dec = lv_image_decoder_create();
  if (dec == NULL) {
    ESP_LOGE(TAG, "Failed to create image decoder");
    return;
  }
  lv_image_decoder_set_info_cb(dec, asset_decoder_info);
  lv_image_decoder_set_open_cb(dec, asset_decoder_open);
  lv_image_decoder_set_close_cb(dec, asset_decoder_close);
  s_decoder_registered = true;
}

void assets_manager_init(void) {
  ESP_LOGI(TAG, "Starting assets manager...");

  if (!esp_littlefs_mounted("assets")) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/assets",
        .partition_label = "assets",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to mount /assets LittleFS (%s)", esp_err_to_name(err));
    } else {
      size_t total = 0, used = 0;
      if (esp_littlefs_info("assets", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "/assets mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
      }
    }
  }

  register_decoder();

  ESP_LOGI(TAG, "Assets manager ready.");
}

lv_image_dsc_t *assets_get(const char *path) {
  if (path == NULL)
    return NULL;

  asset_node_t *node = find_node_by_path(path);
  if (node != NULL)
    return &node->dsc;

  char eff[ASSETS_FILE_MAX];
  resolve_file(path, eff, sizeof(eff));

  bin_header_t hdr;
  if (!read_bin_header(eff, &hdr)) {
    ESP_LOGW(TAG, "asset not found: %s", eff);
    return NULL;
  }

  node = calloc(1, sizeof(asset_node_t));
  if (node == NULL)
    return NULL;
  node->path = strdup(path);
  node->file = strdup(eff);
  if (node->path == NULL || node->file == NULL) {
    free(node->path);
    free(node->file);
    free(node);
    return NULL;
  }

  header_to_dsc(&hdr, &node->dsc);
  node->dsc.data = (const uint8_t *)node->file;
  load_sd_pixels(node);

  node->next = s_assets_head;
  s_assets_head = node;
  return &node->dsc;
}

void assets_manager_free_all(void) {
  asset_node_t *curr = s_assets_head;
  while (curr) {
    asset_node_t *next = curr->next;
    free(curr->path);
    free(curr->file);
    free(curr->sd_data);
    free(curr);
    curr = next;
  }
  s_assets_head = NULL;
}

// Drop the LVGL image cache (the decoded-pixel pool, up to CONFIG_LV_CACHE_DEF_SIZE)
// under memory pressure. Safe to call mid-session, unlike free_all: the asset
// nodes and the &node->dsc pointers the UI holds stay valid, so a redraw just
// re-decodes from LittleFS. Takes the LVGL lock; skips if the UI is mid-render.
#define ASSETS_EVICT_LOCK_MS 200
void assets_manager_evict_cache(void) {
  if (!lvgl_glue_lock(ASSETS_EVICT_LOCK_MS)) {
    ESP_LOGW(TAG, "evict skipped: LVGL lock busy");
    return;
  }
  lv_image_cache_drop(NULL);
  lvgl_glue_unlock();
}

int assets_load_from_sd(const char *sd_dir, const char *flash_prefix) {
  if (sd_dir == NULL || flash_prefix == NULL || s_override_count >= ASSETS_OVERRIDE_MAX)
    return 0;
  DIR *d = opendir(sd_dir);
  if (d == NULL)
    return 0;

  assets_override_t *ov = &s_overrides[s_override_count];
  strlcpy(ov->flash_prefix, flash_prefix, ASSETS_PREFIX_MAX);
  strlcpy(ov->sd_dir, sd_dir, ASSETS_SDDIR_MAX);
  ov->files = NULL;
  ov->file_count = 0;

  int cap = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_type == DT_DIR)
      continue;
    const char *extension = strrchr(de->d_name, '.');
    if (extension != NULL && strcmp(extension, ".license") == 0)
      continue;
    if (de->d_name[0] == '.' &&
        (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0')))
      continue;
    if (ov->file_count == cap) {
      int ncap = cap ? cap * 2 : ASSETS_FILELIST_INIT;
      char **nf = realloc(ov->files, (size_t)ncap * sizeof(char *));
      if (nf == NULL) {
        ESP_LOGE(TAG, "override file index realloc failed");
        break;
      }
      ov->files = nf;
      cap = ncap;
    }
    ov->files[ov->file_count] = strdup(de->d_name);
    if (ov->files[ov->file_count] == NULL) {
      ESP_LOGE(TAG, "override filename strdup failed");
      break;
    }
    ov->file_count++;
  }
  closedir(d);

  s_override_count++;
  refresh_overridden_nodes();
  return 1;
}

void assets_manager_warm(const char *flash_prefix) {
  for (int i = 0; i < s_override_count; i++) {
    if (flash_prefix != NULL && strcmp(s_overrides[i].flash_prefix, flash_prefix) != 0)
      continue;
    for (int j = 0; j < s_overrides[i].file_count; j++) {
      char p[ASSETS_FILE_MAX];
      int n = snprintf(p, sizeof(p), "%s/%s", s_overrides[i].flash_prefix, s_overrides[i].files[j]);
      if (n > 0 && (size_t)n < sizeof(p))
        assets_get(p);
    }
  }
}

static void free_override_index(void) {
  for (int i = 0; i < s_override_count; i++) {
    for (int j = 0; j < s_overrides[i].file_count; j++)
      free(s_overrides[i].files[j]);
    free(s_overrides[i].files);
    s_overrides[i].files = NULL;
    s_overrides[i].file_count = 0;
  }
}

void assets_unload_sd(void) {
  if (s_override_count == 0)
    return;
  free_override_index();
  s_override_count = 0;
  refresh_overridden_nodes();
}
