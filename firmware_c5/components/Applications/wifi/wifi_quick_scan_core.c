// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_quick_scan_core.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "mac_vendor.h"

#define FRAME_VERSION_MASK            0x03
#define FRAME_TYPE_MASK               0x0C
#define FRAME_TYPE_CONTROL            0x04
#define FRAME_TYPE_DATA               0x08
#define FRAME_TYPE_EXTENSION          0x0C
#define FRAME_SUBTYPE_MASK            0xF0
#define FRAME_BEACON                  0x80
#define FRAME_PROBE_REQUEST           0x40
#define FRAME_PROBE_RESPONSE          0x50
#define FRAME_DS_MASK                 0x03
#define FRAME_TO_DS                   0x01
#define FRAME_FROM_DS                 0x02
#define FRAME_RECEIVER_OFFSET         4
#define FRAME_BSSID_OFFSET            16
#define FRAME_IE_OFFSET               36
#define SSID_IE_ID                    0
#define IE_HEADER_LEN                 2
#define PRINTABLE_MIN                 0x20
#define PRINTABLE_MAX                 0x7E
#define FRAME_ACTION                  0xD0
#define FRAME_PROTECTED               0x40
#define FRAME_ORDER                   0x80
#define FRAME_MORE_FRAGMENTS          0x04
#define FRAME_FRAGMENT_MASK           0x0F
#define FRAME_SEQUENCE_OFFSET         22
#define FRAME_HEADER_LEN              24
#define FRAME_CONTROL_TA_LEN          16
#define FRAME_RTS                     0xB0
#define FRAME_BLOCK_ACK_REQUEST       0x80
#define FRAME_BLOCK_ACK               0x90
#define FRAME_PS_POLL                 0xA0
#define FRAME_CF_END                  0xE0
#define FRAME_CF_END_ACK              0xF0
#define FRAME_TRANSMITTER_OFFSET      10
#define MAC_LEN                       SPI_WIFI_QUICK_MAC_LEN
#define MAC_GROUP_BIT                 0x01
#define MAC_LOCAL_BIT                 0x02
#define ESPNOW_CATEGORY               127
#define ESPNOW_ELEMENT_ID             221
#define ESPNOW_TYPE                   4
#define ESPNOW_VERSION_MASK           0x0F
#define ESPNOW_VERSION_1              1
#define ESPNOW_VERSION_2              2
#define ESPNOW_ELEMENT_OFFSET         32
#define ESPNOW_ELEMENT_MIN_LEN        5
#define ESPNOW_ELEMENT_HEADER_LEN     2
#define ESPNOW_ELEMENT_OUI_OFFSET     2
#define ESPNOW_ELEMENT_TYPE_OFFSET    5
#define ESPNOW_ELEMENT_VERSION_OFFSET 6
#define ESPNOW_MIN_FRAME_LEN          39
#define SCORE_OUI                     45
#define SCORE_FINGERPRINT             25
#define SCORE_PROXIMITY               20
#define SCORE_PERSISTENCE             10
#define RSSI_STRONG_DBM               (-55)
#define RSSI_NEAR_DBM                 (-65)
#define RSSI_SPREAD_DB                12
#define RSSI_MIN_SAMPLES              3
#define PERSISTENCE_MIN_WINDOWS       3
#define WINDOW_BITS                   16
#define HALF                          2

static const char *TAG = "WIFI_QUICK_SCAN_CORE";
static const uint8_t ESPRESSIF_OUI[] = {0x18, 0xFE, 0x34};

static bool has_espnow_signature(const uint8_t *frame, size_t length);
static int compare_records(const void *left, const void *right);
static void parse_network(const uint8_t *frame,
                          size_t length,
                          wifi_quick_scan_core_observation_t *out_observation);

esp_err_t wifi_quick_scan_core_parse(const uint8_t *frame,
                                     size_t length,
                                     wifi_quick_scan_core_observation_t *out_observation) {
  (void)TAG;
  if (frame == NULL || out_observation == NULL || length < FRAME_CONTROL_TA_LEN ||
      (frame[0] & FRAME_VERSION_MASK) != 0) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t type = frame[0] & FRAME_TYPE_MASK;
  uint8_t subtype = frame[0] & FRAME_SUBTYPE_MASK;
  if (type == FRAME_TYPE_EXTENSION || (type != FRAME_TYPE_CONTROL && length < FRAME_HEADER_LEN)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (type == FRAME_TYPE_CONTROL && subtype != FRAME_RTS && subtype != FRAME_BLOCK_ACK_REQUEST &&
      subtype != FRAME_BLOCK_ACK && subtype != FRAME_PS_POLL && subtype != FRAME_CF_END &&
      subtype != FRAME_CF_END_ACK) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  const uint8_t *mac = frame + FRAME_TRANSMITTER_OFFSET;
  static const uint8_t ZERO_MAC[MAC_LEN] = {0};
  if ((mac[0] & MAC_GROUP_BIT) != 0 || memcmp(mac, ZERO_MAC, MAC_LEN) == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  memset(out_observation, 0, sizeof(*out_observation));
  memcpy(out_observation->mac, mac, MAC_LEN);
  uint16_t evidence = SPI_WIFI_QUICK_OTHER;
  if (type == 0 && subtype == FRAME_BEACON) {
    evidence = SPI_WIFI_QUICK_BEACON;
  } else if (type == 0 && subtype == FRAME_PROBE_REQUEST) {
    evidence = SPI_WIFI_QUICK_PROBE;
  }
  if ((mac[0] & MAC_LOCAL_BIT) != 0) {
    evidence |= SPI_WIFI_QUICK_LOCAL_MAC;
  } else if (strcmp(mac_vendor_get_name(mac), "Espressif") == 0) {
    evidence |= SPI_WIFI_QUICK_ESPRESSIF_OUI;
  }
  if (type == 0 && subtype == FRAME_ACTION && has_espnow_signature(frame, length)) {
    evidence |= SPI_WIFI_QUICK_ESPNOW;
  }
  out_observation->evidence = evidence;
  parse_network(frame, length, out_observation);
  return ESP_OK;
}

void wifi_quick_scan_core_observe(wifi_quick_scan_core_t *core,
                                  const wifi_quick_scan_core_observation_t *observation) {
  size_t index = 0;
  for (; index < core->count; index++) {
    if (memcmp(core->entries[index].record.mac, observation->mac, MAC_LEN) == 0) {
      break;
    }
  }
  if (index == core->count) {
    if (core->count == WIFI_QUICK_SCAN_MAX_RECORDS) {
      if (core->overflow < UINT32_MAX) {
        core->overflow++;
      }
      return;
    }
    core->count++;
    wifi_quick_scan_core_entry_t *entry = &core->entries[index];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->record.mac, observation->mac, MAC_LEN);
    entry->record.first_seen_ms = observation->elapsed_ms;
    entry->record.rssi_min = observation->rssi;
    entry->record.rssi_max = observation->rssi;
  }
  wifi_quick_scan_core_entry_t *entry = &core->entries[index];
  spi_wifi_quick_record_t *record = &entry->record;
  static const uint8_t ZERO_BSSID[MAC_LEN] = {0};
  if (memcmp(observation->bssid, ZERO_BSSID, MAC_LEN) != 0) {
    if (memcmp(record->bssid, observation->bssid, MAC_LEN) != 0) {
      record->ssid_state = SPI_WIFI_QUICK_SSID_UNKNOWN;
      memset(record->ssid, 0, sizeof(record->ssid));
      memcpy(record->bssid, observation->bssid, MAC_LEN);
    }
    if (observation->ssid_state != SPI_WIFI_QUICK_SSID_UNKNOWN &&
        (observation->ssid_state == SPI_WIFI_QUICK_SSID_KNOWN ||
         record->ssid_state != SPI_WIFI_QUICK_SSID_KNOWN)) {
      memcpy(record->ssid, observation->ssid, sizeof(record->ssid));
      record->ssid_state = observation->ssid_state;
    }
  }
  if (record->frames == UINT32_MAX) {
    return;
  }
  record->frames++;
  entry->rssi_sum += observation->rssi;
  record->rssi_avg = (int8_t)(entry->rssi_sum / record->frames);
  if (observation->rssi < record->rssi_min) {
    record->rssi_min = observation->rssi;
  }
  if (observation->rssi > record->rssi_max) {
    record->rssi_max = observation->rssi;
  }
  record->last_seen_ms = observation->elapsed_ms;
  record->channel = observation->channel;
  record->evidence |= observation->evidence;
  uint32_t window = observation->elapsed_ms / WIFI_QUICK_SCAN_WINDOW_MS;
  if (window < WINDOW_BITS) {
    entry->windows |= (uint16_t)(1u << window);
  }
}

void wifi_quick_scan_core_finish(wifi_quick_scan_core_t *core,
                                 uint32_t duration_ms,
                                 const uint8_t (*whitelist)[MAC_LEN],
                                 size_t whitelist_count) {
  for (size_t i = 0; i < core->count; i++) {
    wifi_quick_scan_core_entry_t *entry = &core->entries[i];
    spi_wifi_quick_record_t *record = &entry->record;
    if (record->ssid_state == SPI_WIFI_QUICK_SSID_UNKNOWN) {
      for (size_t j = 0; j < core->count; j++) {
        const spi_wifi_quick_record_t *network = &core->entries[j].record;
        if (network->ssid_state != SPI_WIFI_QUICK_SSID_UNKNOWN &&
            memcmp(record->bssid, network->bssid, MAC_LEN) == 0) {
          memcpy(record->ssid, network->ssid, sizeof(record->ssid));
          record->ssid_state = network->ssid_state;
          break;
        }
      }
    }
    record->is_authorized = false;
    for (size_t j = 0; j < whitelist_count; j++) {
      if (memcmp(record->mac, whitelist[j], MAC_LEN) == 0) {
        record->is_authorized = true;
        break;
      }
    }
    const char *vendor = mac_vendor_get_name(record->mac);
    if (vendor[0] == '\0') {
      vendor = "Unknown";
    } else if ((record->evidence & SPI_WIFI_QUICK_LOCAL_MAC) != 0) {
      vendor = "Local/unknown";
    }
    strncpy(record->vendor, vendor, sizeof(record->vendor) - 1);
    record->vendor[sizeof(record->vendor) - 1] = '\0';
    record->oui_points = (record->evidence & SPI_WIFI_QUICK_ESPRESSIF_OUI) != 0 ? SCORE_OUI : 0;
    record->fingerprint_points =
        (record->evidence & SPI_WIFI_QUICK_ESPNOW) != 0 ? SCORE_FINGERPRINT : 0;
    record->proximity_points = 0;
    if (record->frames >= RSSI_MIN_SAMPLES && record->rssi_avg >= RSSI_NEAR_DBM &&
        record->rssi_max - record->rssi_min <= RSSI_SPREAD_DB) {
      record->proximity_points =
          record->rssi_avg >= RSSI_STRONG_DBM ? SCORE_PROXIMITY : SCORE_PROXIMITY / HALF;
    }
    uint16_t windows = entry->windows;
    uint8_t window_count = 0;
    while (windows != 0) {
      window_count += windows & 1u;
      windows >>= 1;
    }
    record->observation_windows = window_count;
    record->persistence_points =
        window_count >= PERSISTENCE_MIN_WINDOWS &&
                record->last_seen_ms - record->first_seen_ms >= duration_ms / HALF
            ? SCORE_PERSISTENCE
            : 0;
    uint8_t identity_points = record->oui_points > record->fingerprint_points
                                  ? record->oui_points
                                  : record->fingerprint_points;
    record->score = record->is_authorized
                        ? 0
                        : identity_points + record->proximity_points + record->persistence_points;
    record->should_investigate = !record->is_authorized && identity_points != 0;
  }
  qsort(core->entries, core->count, sizeof(core->entries[0]), compare_records);
}

static void parse_network(const uint8_t *frame,
                          size_t length,
                          wifi_quick_scan_core_observation_t *out_observation) {
  uint8_t type = frame[0] & FRAME_TYPE_MASK;
  uint8_t subtype = frame[0] & FRAME_SUBTYPE_MASK;
  const uint8_t *bssid = NULL;
  if (type == 0 && (subtype == FRAME_BEACON || subtype == FRAME_PROBE_RESPONSE)) {
    bssid = frame + FRAME_BSSID_OFFSET;
  } else if (type == FRAME_TYPE_DATA) {
    uint8_t ds = frame[1] & FRAME_DS_MASK;
    if (ds == FRAME_TO_DS) {
      bssid = frame + FRAME_RECEIVER_OFFSET;
    } else if (ds == FRAME_FROM_DS) {
      bssid = frame + FRAME_TRANSMITTER_OFFSET;
    } else if (ds == 0) {
      bssid = frame + FRAME_BSSID_OFFSET;
    }
  }
  if (bssid == NULL || (bssid[0] & MAC_GROUP_BIT) != 0) {
    return;
  }
  memcpy(out_observation->bssid, bssid, MAC_LEN);
  if (type != 0 || (subtype != FRAME_BEACON && subtype != FRAME_PROBE_RESPONSE) ||
      (frame[1] & (FRAME_PROTECTED | FRAME_ORDER | FRAME_MORE_FRAGMENTS)) != 0 ||
      (frame[FRAME_SEQUENCE_OFFSET] & FRAME_FRAGMENT_MASK) != 0) {
    return;
  }
  size_t offset = FRAME_IE_OFFSET;
  while (offset <= length && length - offset >= IE_HEADER_LEN) {
    uint8_t id = frame[offset];
    size_t size = frame[offset + 1];
    offset += IE_HEADER_LEN;
    if (size > length - offset) {
      return;
    }
    if (id == SSID_IE_ID) {
      if (size >= sizeof(out_observation->ssid)) {
        return;
      }
      bool has_name = false;
      for (size_t i = 0; i < size; i++) {
        uint8_t byte = frame[offset + i];
        has_name |= byte != 0;
        out_observation->ssid[i] = byte >= PRINTABLE_MIN && byte <= PRINTABLE_MAX ? byte : '?';
      }
      out_observation->ssid_state =
          has_name ? SPI_WIFI_QUICK_SSID_KNOWN : SPI_WIFI_QUICK_SSID_HIDDEN;
      return;
    }
    offset += size;
  }
}

static bool has_espnow_signature(const uint8_t *frame, size_t length) {
  if (length < ESPNOW_MIN_FRAME_LEN ||
      (frame[1] & (FRAME_PROTECTED | FRAME_ORDER | FRAME_MORE_FRAGMENTS)) != 0 ||
      (frame[FRAME_SEQUENCE_OFFSET] & FRAME_FRAGMENT_MASK) != 0 ||
      frame[FRAME_HEADER_LEN] != ESPNOW_CATEGORY ||
      memcmp(frame + FRAME_HEADER_LEN + 1, ESPRESSIF_OUI, sizeof(ESPRESSIF_OUI)) != 0) {
    return false;
  }
  const uint8_t *element = frame + ESPNOW_ELEMENT_OFFSET;
  uint8_t version = element[ESPNOW_ELEMENT_VERSION_OFFSET] & ESPNOW_VERSION_MASK;
  return element[0] == ESPNOW_ELEMENT_ID && element[1] >= ESPNOW_ELEMENT_MIN_LEN &&
         (size_t)element[1] <= length - ESPNOW_ELEMENT_OFFSET - ESPNOW_ELEMENT_HEADER_LEN &&
         memcmp(element + ESPNOW_ELEMENT_OUI_OFFSET, ESPRESSIF_OUI, sizeof(ESPRESSIF_OUI)) == 0 &&
         element[ESPNOW_ELEMENT_TYPE_OFFSET] == ESPNOW_TYPE &&
         (version == ESPNOW_VERSION_1 || version == ESPNOW_VERSION_2);
}

static int compare_records(const void *left, const void *right) {
  const wifi_quick_scan_core_entry_t *a = left;
  const wifi_quick_scan_core_entry_t *b = right;
  if (a->record.is_authorized != b->record.is_authorized) {
    return a->record.is_authorized ? 1 : -1;
  }
  if (a->record.score != b->record.score) {
    return (int)b->record.score - (int)a->record.score;
  }
  return memcmp(a->record.mac, b->record.mac, MAC_LEN);
}
