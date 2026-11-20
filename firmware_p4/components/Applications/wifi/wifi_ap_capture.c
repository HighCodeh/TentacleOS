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

#include "wifi_ap_capture.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "pcap_serializer.h"
#include "led_control.h"
#include "storage_init.h"
#include "storage_mkdir.h"
#include "tos_storage_paths.h"

#define AP_CACHE_COUNT        32
#define AP_FRAME_MAX          2048
#define AP_PATH_MAX           256
#define AP_SSID_MAX           32
#define AP_MAC_LEN            6
#define AP_SUFFIX_SIZE        19
#define MAC_HEADER_LEN        24
#define MAC_ADDR1_OFFSET      4
#define MAC_ADDR2_OFFSET      10
#define MAC_ADDR3_OFFSET      16
#define FRAME_TYPE_MASK       0x0C
#define FRAME_DATA            0x08
#define FRAME_SUBTYPE_MASK    0xF0
#define FRAME_BEACON          0x80
#define FRAME_PROBE_RESPONSE  0x50
#define FRAME_ASSOC_REQUEST   0x00
#define FRAME_REASSOC_REQUEST 0x20
#define FRAME_DS_MASK         0x03
#define FRAME_TO_DS           0x01
#define FRAME_FROM_DS         0x02
#define FRAME_QOS             0x80
#define FRAME_PROTECTED       0x40
#define FRAME_ORDER           0x80
#define QOS_HEADER_LEN        2
#define HT_HEADER_LEN         4
#define FRAME_FCS_LEN         4
#define BEACON_FIXED_LEN      12
#define ASSOC_FIXED_LEN       4
#define REASSOC_FIXED_LEN     10
#define IE_HEADER_LEN         2
#define EAPOL_HEADER_LEN      4
#define LLC_HEADER_LEN        8

static const char *TAG = "WIFI_AP_CAPTURE";

typedef struct {
  bool is_used;
  bool has_ssid;
  bool has_file;
  bool has_metadata_written;
  uint8_t bssid[AP_MAC_LEN];
  char name[AP_SSID_MAX + 1];
  char path[AP_PATH_MAX];
  uint8_t metadata[AP_FRAME_MAX];
  size_t metadata_len;
  struct timeval metadata_at;
  struct timeval frame_at;
} wifi_ap_capture_entry_t;

static wifi_ap_capture_entry_t *s_entries;
static size_t s_next_slot;
static size_t s_bytes;
static wifi_ap_capture_snapshot_t s_saved;

static wifi_ap_capture_entry_t *find_ap(const uint8_t *bssid);
static bool read_ssid(const uint8_t *frame, size_t len, size_t offset, char *out_name);
static esp_err_t select_path(wifi_ap_capture_entry_t *ap);
static esp_err_t append_frame(wifi_ap_capture_entry_t *ap, const uint8_t *frame, size_t len);
static esp_err_t rename_hidden(wifi_ap_capture_entry_t *ap);

esp_err_t wifi_ap_capture_start(void) {
  if (s_entries != NULL)
    return ESP_ERR_INVALID_STATE;
  if (!storage_is_mounted())
    return ESP_ERR_INVALID_STATE;
  esp_err_t result = storage_mkdir_recursive(TOS_PATH_WIFI_LOOT_HS);
  if (result != ESP_OK)
    return result;
  s_entries =
      heap_caps_calloc(AP_CACHE_COUNT, sizeof(*s_entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_entries == NULL) {
    ESP_LOGE(TAG, "AP metadata cache allocation failed");
    return ESP_ERR_NO_MEM;
  }
  s_next_slot = 0;
  s_bytes = 0;
  memset(&s_saved, 0, sizeof(s_saved));
  ESP_LOGI(TAG, "Per-AP capture ready: %s", TOS_PATH_WIFI_LOOT_HS);
  return ESP_OK;
}

void wifi_ap_capture_stop(void) {
  free(s_entries);
  s_entries = NULL;
}

void wifi_ap_capture_snapshot(wifi_ap_capture_snapshot_t *out_snapshot) {
  *out_snapshot = s_saved;
  out_snapshot->bytes = s_bytes;
}

size_t wifi_ap_capture_bytes(void) {
  return s_bytes;
}

esp_err_t wifi_ap_capture_frame(const uint8_t *frame, size_t len) {
  struct timeval now;
  gettimeofday(&now, NULL);
  return wifi_ap_capture_frame_at(frame, len, &now);
}

esp_err_t
wifi_ap_capture_frame_at(const uint8_t *frame, size_t len, const struct timeval *received_at) {
  if (received_at == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_entries == NULL)
    return ESP_ERR_INVALID_STATE;
  if (frame == NULL || len < MAC_HEADER_LEN + FRAME_FCS_LEN || len > AP_FRAME_MAX)
    return ESP_OK;
  size_t packet_len = len - FRAME_FCS_LEN;
  uint8_t type = frame[0] & FRAME_TYPE_MASK;
  uint8_t subtype = frame[0] & FRAME_SUBTYPE_MASK;
  if (type == 0) {
    size_t offset;
    switch (subtype) {
      case FRAME_BEACON:
      case FRAME_PROBE_RESPONSE:
        offset = MAC_HEADER_LEN + BEACON_FIXED_LEN;
        break;
      case FRAME_ASSOC_REQUEST:
        offset = MAC_HEADER_LEN + ASSOC_FIXED_LEN;
        break;
      case FRAME_REASSOC_REQUEST:
        offset = MAC_HEADER_LEN + REASSOC_FIXED_LEN;
        break;
      default:
        return ESP_OK;
    }
    if (packet_len < offset)
      return ESP_OK;
    char name[AP_SSID_MAX + 1] = {0};
    if (!read_ssid(frame, packet_len, offset, name))
      return ESP_OK;
    wifi_ap_capture_entry_t *ap = find_ap(frame + MAC_ADDR3_OFFSET);
    if (ap->has_ssid)
      return ESP_OK;
    ap->has_ssid = true;
    memcpy(ap->name, name, sizeof(ap->name));
    memcpy(ap->metadata, frame, packet_len);
    ap->metadata_len = packet_len;
    ap->metadata_at = *received_at;
    ap->has_metadata_written = false;
    if (ap->has_file) {
      esp_err_t result = rename_hidden(ap);
      if (result != ESP_OK)
        return result;
      return append_frame(ap, NULL, 0);
    }
    return ESP_OK;
  }
  uint8_t ds = frame[1] & FRAME_DS_MASK;
  if (type != FRAME_DATA || (frame[1] & FRAME_PROTECTED) ||
      (ds != FRAME_TO_DS && ds != FRAME_FROM_DS))
    return ESP_OK;
  size_t offset = MAC_HEADER_LEN;
  if (frame[0] & FRAME_QOS) {
    offset += QOS_HEADER_LEN;
    if (frame[1] & FRAME_ORDER)
      offset += HT_HEADER_LEN;
  }
  static const uint8_t EAPOL_SNAP[LLC_HEADER_LEN] = {
      0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  if (packet_len < offset + LLC_HEADER_LEN + EAPOL_HEADER_LEN ||
      memcmp(frame + offset, EAPOL_SNAP, LLC_HEADER_LEN) != 0)
    return ESP_OK;
  wifi_ap_capture_entry_t *ap =
      find_ap(frame + (ds == FRAME_FROM_DS ? MAC_ADDR2_OFFSET : MAC_ADDR1_OFFSET));
  ap->frame_at = *received_at;
  return append_frame(ap, frame, packet_len);
}

static wifi_ap_capture_entry_t *find_ap(const uint8_t *bssid) {
  for (size_t i = 0; i < AP_CACHE_COUNT; i++) {
    if (s_entries[i].is_used && memcmp(s_entries[i].bssid, bssid, AP_MAC_LEN) == 0)
      return &s_entries[i];
  }
  wifi_ap_capture_entry_t *ap = &s_entries[s_next_slot];
  s_next_slot = (s_next_slot + 1) % AP_CACHE_COUNT;
  memset(ap, 0, sizeof(*ap));
  ap->is_used = true;
  memcpy(ap->bssid, bssid, AP_MAC_LEN);
  return ap;
}

static bool read_ssid(const uint8_t *frame, size_t len, size_t offset, char *out_name) {
  while (offset + IE_HEADER_LEN <= len) {
    uint8_t tag = frame[offset];
    size_t size = frame[offset + 1];
    offset += IE_HEADER_LEN;
    if (size > len - offset)
      return false;
    if (tag == 0) {
      if (size == 0 || size > AP_SSID_MAX)
        return false;
      bool has_visible_byte = false;
      for (size_t i = 0; i < size; i++) {
        uint8_t ch = frame[offset + i];
        has_visible_byte |= ch != 0;
        out_name[i] = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')
                          ? ch
                          : '_';
      }
      out_name[size] = '\0';
      return has_visible_byte;
    }
    offset += size;
  }
  return false;
}

static void make_suffix(const uint8_t *mac, char *out_suffix) {
  snprintf(out_suffix,
           AP_SUFFIX_SIZE,
           "_%02X%02X%02X%02X%02X%02X.pcap",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5]);
}

static esp_err_t validate_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return ESP_FAIL;
  pcap_global_header_t header;
  esp_err_t result = ESP_ERR_INVALID_RESPONSE;
  if (fread(&header, 1, sizeof(header), file) != sizeof(header) ||
      header.magic_number != PCAP_MAGIC_NUMBER || header.version_major != PCAP_VERSION_MAJOR ||
      header.version_minor != PCAP_VERSION_MINOR || header.network != PCAP_LINK_TYPE_802_11 ||
      header.snaplen != AP_FRAME_MAX)
    goto cleanup;
  if (fseek(file, 0, SEEK_END) != 0)
    goto cleanup;
  long end = ftell(file);
  if (end < (long)sizeof(header) || fseek(file, sizeof(header), SEEK_SET) != 0)
    goto cleanup;
  while (ftell(file) < end) {
    pcap_packet_header_t record;
    if (fread(&record, 1, sizeof(record), file) != sizeof(record) || record.incl_len == 0 ||
        record.incl_len > AP_FRAME_MAX || record.incl_len != record.orig_len ||
        record.incl_len > (size_t)(end - ftell(file)) ||
        fseek(file, record.incl_len, SEEK_CUR) != 0)
      goto cleanup;
  }
  result = ESP_OK;
cleanup:
  fclose(file);
  return result;
}

static esp_err_t select_path(wifi_ap_capture_entry_t *ap) {
  char suffix[AP_SUFFIX_SIZE];
  make_suffix(ap->bssid, suffix);
  DIR *directory = opendir(TOS_PATH_WIFI_LOOT_HS);
  if (directory == NULL)
    return ESP_FAIL;
  struct dirent *entry;
  bool has_existing = false;
  while ((entry = readdir(directory)) != NULL) {
    size_t len = strlen(entry->d_name);
    size_t suffix_len = strlen(suffix);
    if (len > suffix_len && strcmp(entry->d_name + len - suffix_len, suffix) == 0) {
      int count =
          snprintf(ap->path, sizeof(ap->path), "%s/%s", TOS_PATH_WIFI_LOOT_HS, entry->d_name);
      if (count < 0 || (size_t)count >= sizeof(ap->path)) {
        closedir(directory);
        return ESP_ERR_INVALID_SIZE;
      }
      has_existing = true;
      break;
    }
  }
  closedir(directory);
  if (has_existing) {
    esp_err_t result = validate_file(ap->path);
    if (result != ESP_OK) {
      ESP_LOGE(TAG, "Refusing to append invalid PCAP: %s", ap->path);
      return result;
    }
  } else {
    snprintf(ap->path,
             sizeof(ap->path),
             "%s/%s%s",
             TOS_PATH_WIFI_LOOT_HS,
             ap->has_ssid ? ap->name : "hidden",
             suffix);
    FILE *file = fopen(ap->path, "wb");
    if (file == NULL)
      return ESP_FAIL;
    const pcap_global_header_t header = {
        .magic_number = PCAP_MAGIC_NUMBER,
        .version_major = PCAP_VERSION_MAJOR,
        .version_minor = PCAP_VERSION_MINOR,
        .snaplen = AP_FRAME_MAX,
        .network = PCAP_LINK_TYPE_802_11,
    };
    bool is_ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    if (fclose(file) != 0)
      is_ok = false;
    if (!is_ok)
      return ESP_FAIL;
  }
  ap->has_file = true;
  esp_err_t result = rename_hidden(ap);
  if (result == ESP_OK)
    ESP_LOGI(TAG, "AP capture: %s%s", ap->path, ap->has_ssid ? "" : " (SSID pending)");
  return result;
}

static esp_err_t rename_hidden(wifi_ap_capture_entry_t *ap) {
  const char *name = strrchr(ap->path, '/');
  if (!ap->has_ssid || name == NULL || strncmp(name + 1, "hidden_", strlen("hidden_")) != 0)
    return ESP_OK;
  char suffix[AP_SUFFIX_SIZE], path[AP_PATH_MAX];
  make_suffix(ap->bssid, suffix);
  snprintf(path, sizeof(path), "%s/%s%s", TOS_PATH_WIFI_LOOT_HS, ap->name, suffix);
  if (strcmp(path, ap->path) == 0)
    return ESP_OK;
  struct stat info;
  if (stat(path, &info) == 0 || errno != ENOENT)
    return ESP_FAIL;
  if (rename(ap->path, path) != 0)
    return ESP_FAIL;
  memcpy(ap->path, path, strlen(path) + 1);
  ESP_LOGI(TAG, "AP identified: %s", ap->path);
  return ESP_OK;
}

static bool
write_record(FILE *file, const uint8_t *frame, size_t len, const struct timeval *received_at) {
  const pcap_packet_header_t record = {
      .ts_sec = (uint32_t)received_at->tv_sec,
      .ts_usec = (uint32_t)received_at->tv_usec,
      .incl_len = (uint32_t)len,
      .orig_len = (uint32_t)len,
  };
  return fwrite(&record, 1, sizeof(record), file) == sizeof(record) &&
         fwrite(frame, 1, len, file) == len;
}

static esp_err_t append_frame(wifi_ap_capture_entry_t *ap, const uint8_t *frame, size_t len) {
  if (!ap->has_file) {
    esp_err_t result = select_path(ap);
    if (result != ESP_OK)
      return result;
  }
  FILE *file = fopen(ap->path, "ab");
  if (file == NULL)
    return ESP_FAIL;
  bool is_ok = true;
  size_t written = 0;
  if (ap->metadata_len != 0 && !ap->has_metadata_written) {
    is_ok = write_record(file, ap->metadata, ap->metadata_len, &ap->metadata_at);
    if (is_ok)
      written += sizeof(pcap_packet_header_t) + ap->metadata_len;
  }
  if (is_ok && frame != NULL) {
    is_ok = write_record(file, frame, len, &ap->frame_at);
    if (is_ok)
      written += sizeof(pcap_packet_header_t) + len;
  }
  if (fclose(file) != 0)
    is_ok = false;
  if (!is_ok) {
    ESP_LOGE(TAG, "AP capture write/close failed: %s", ap->path);
    return ESP_FAIL;
  }
  ap->has_metadata_written = ap->metadata_len != 0;
  s_bytes += written;
  size_t index = 0;
  while (index < s_saved.count && memcmp(s_saved.recent[index].bssid, ap->bssid, AP_MAC_LEN) != 0)
    index++;
  bool is_new = index == s_saved.count;
  if (frame == NULL && is_new)
    return ESP_OK;
  wifi_ap_capture_saved_t saved = {0};
  if (!is_new)
    saved = s_saved.recent[index];
  else {
    if (s_saved.count < WIFI_AP_CAPTURE_RECENT_COUNT)
      s_saved.count++;
    index = s_saved.count - 1;
    memcpy(saved.bssid, ap->bssid, AP_MAC_LEN);
  }
  if (frame != NULL) {
    saved.frames++;
    s_saved.frames++;
  }
  saved.has_ssid = ap->has_ssid;
  const char *basename = strrchr(ap->path, '/');
  snprintf(saved.filename,
           sizeof(saved.filename),
           "%.*s",
           (int)sizeof(saved.filename) - 1,
           basename ? basename + 1 : ap->path);
  memmove(&s_saved.recent[1], &s_saved.recent[0], index * sizeof(saved));
  s_saved.recent[0] = saved;
  if (is_new) {
    ESP_LOGI(TAG, "EAPOL saved: %s", ap->path);
    led_signal_info();
  }
  return ESP_OK;
}
