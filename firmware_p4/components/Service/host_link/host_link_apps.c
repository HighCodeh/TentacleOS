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

#include "host_link_apps.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "spi_protocol.h"
#include "tos_api.h"
#include "tos_app_mgr.h"
#include "tos_grants.h"
#include "tos_hb.h"

#define APPS_DIR      "/sdcard/apps"
#define APP_MAX_BYTES (512 * 1024)

// Copy the payload (raw app-name bytes) into a sanitized, null-terminated name.
static bool get_name(const uint8_t *payload, uint16_t plen, char *out, size_t cap) {
  if (plen == 0 || (size_t)plen >= cap)
    return false;
  for (uint16_t i = 0; i < plen; i++) {
    char c = (char)payload[i];
    if (c == '/' || c == '\\' || c == '\0')
      return false;
    out[i] = c;
  }
  out[plen] = '\0';
  return strstr(out, "..") == NULL;
}

static uint8_t *read_hb(const char *name, size_t *out_len) {
  char path[80];
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
  uint8_t *b = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (b == NULL) {
    fclose(f);
    return NULL;
  }
  size_t rd = fread(b, 1, (size_t)sz, f);
  fclose(f);
  if (rd != (size_t)sz) {
    heap_caps_free(b);
    return NULL;
  }
  *out_len = (size_t)sz;
  return b;
}

static bool is_running(const char *name) {
  char names[TOS_APP_MAX][TOS_APP_NAME_CAP];
  int n = tos_app_mgr_list(names, TOS_APP_MAX);
  for (int i = 0; i < n; i++)
    if (strcmp(names[i], name) == 0)
      return true;
  return false;
}

// APP_LIST: [count u8] then per app [running u8][granted u32 LE][nlen u8][name].
static uint8_t do_list(uint8_t *out, uint16_t out_cap, uint16_t *out_len) {
  if (out_cap < 1)
    return SPI_STATUS_ERROR;
  uint8_t *p = out;
  uint8_t *count_at = p++;
  uint8_t count = 0;

  DIR *d = opendir(APPS_DIR);
  if (d != NULL) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < 255) {
      const char *fn = ent->d_name;
      size_t l = strlen(fn);
      if (l <= 3 || l - 3 > 15 || strcmp(fn + l - 3, ".hb") != 0)
        continue;
      char name[16];
      memcpy(name, fn, l - 3);
      name[l - 3] = '\0';
      uint8_t nlen = (uint8_t)(l - 3);
      size_t need = 1 + 4 + 1 + nlen;
      if ((size_t)(p - out) + need > out_cap)
        break;
      uint32_t granted = tos_grants_get(name);
      *p++ = is_running(name) ? 1 : 0;
      memcpy(p, &granted, 4);
      p += 4;
      *p++ = nlen;
      memcpy(p, name, nlen);
      p += nlen;
      count++;
    }
    closedir(d);
  }
  *count_at = count;
  *out_len = (uint16_t)(p - out);
  return SPI_STATUS_OK;
}

static uint8_t status_from_start(esp_err_t e) {
  if (e == ESP_OK)
    return SPI_STATUS_OK;
  if (e == ESP_ERR_INVALID_STATE)
    return SPI_STATUS_BUSY; // app slots full
  return SPI_STATUS_ERROR;
}

uint8_t host_apps_handle(uint16_t cmd,
                         const uint8_t *payload,
                         uint16_t plen,
                         uint8_t *out,
                         uint16_t out_cap,
                         uint16_t *out_len) {
  *out_len = 0;

  if (cmd == SPI_ID_APP_LIST)
    return do_list(out, out_cap, out_len);

  char name[16];
  if (!get_name(payload, plen, name, sizeof(name)))
    return SPI_STATUS_INVALID_ARG;

  switch (cmd) {
    case SPI_ID_APP_INSTALL: {
      size_t len;
      uint8_t *hb = read_hb(name, &len);
      if (hb == NULL)
        return SPI_STATUS_ERROR;
      tos_hb_t meta;
      esp_err_t e = tos_hb_open(hb, len, &meta); // verifies the signature
      heap_caps_free(hb);
      return (e == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }
    case SPI_ID_APP_GRANT: {
      size_t len;
      uint8_t *hb = read_hb(name, &len);
      if (hb == NULL)
        return SPI_STATUS_ERROR;
      tos_hb_t meta;
      esp_err_t e = tos_hb_open(hb, len, &meta);
      heap_caps_free(hb);
      if (e != ESP_OK)
        return SPI_STATUS_ERROR;
      return tos_grants_set(name, meta.caps) == ESP_OK ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }
    case SPI_ID_APP_START: {
      size_t len;
      uint8_t *hb = read_hb(name, &len);
      if (hb == NULL)
        return SPI_STATUS_ERROR;
      tos_hb_t meta;
      esp_err_t e = tos_hb_open(hb, len, &meta);
      if (e != ESP_OK) {
        heap_caps_free(hb);
        return SPI_STATUS_ERROR;
      }
      if ((meta.caps & ~tos_grants_get(name)) != 0) {
        heap_caps_free(hb);
        return SPI_STATUS_INVALID_ARG; // needs an APP_GRANT first
      }
      e = tos_app_mgr_start(hb, len, tos_api_get());
      heap_caps_free(hb);
      return status_from_start(e);
    }
    case SPI_ID_APP_STOP:
      return tos_app_mgr_stop(name, 2000) == ESP_OK ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    case SPI_ID_APP_REMOVE: {
      char path[80];
      snprintf(path, sizeof(path), APPS_DIR "/%s.hb", name);
      remove(path);
      tos_grants_clear(name);
      return SPI_STATUS_OK;
    }
    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}
