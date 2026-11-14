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

#include "wifi_sniffer.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "pcap_serializer.h"
#include "sd_card_init.h"
#include "sd_card_write.h"
#include "session_manager.h"
#include "spi_bridge.h"
#include "storage_mkdir.h"
#include "storage_write.h"
#include "tos_flash_paths.h"
#include "wifi_80211.h"
#include "wifi_service.h"
#include "wifi_deauther.h"

static const char *TAG = "WIFI_SNIFFER";

_Static_assert(sizeof(spi_sniffer_stats_t) <= SPI_MAX_PAYLOAD,
               "sniffer statistics exceed one SPI payload");

#define SNIFFER_BUFFER_SIZE       (200 * 1024)
#define SNIFFER_BUFFER_MARGIN     2048
#define SNIFFER_STREAM_CHUNK_SIZE 4096
#define SNIFFER_STREAM_DELAY_MS   50
#define SNIFFER_STOP_DELAY_MS     200
#define SNIFFER_TASK_STACK_SIZE   4096
#define MAX_TRACKED_SESSIONS      16
#define MAX_KNOWN_APS             128
#define DEFAULT_SNAPLEN           65535
#define BSSID_LEN                 6
#define MAC_LEN                   6
#define MAC_HEADER_BASE_LEN       24
#define MAC_HEADER_4ADDR_LEN      30
#define QOS_HEADER_EXTRA          2
#define US_PER_S                  1000000
#define US_PER_MS                 1000
#define BEACON_SUBTYPE            8
#define PROBE_RESP_SUBTYPE        5
#define PROBE_REQ_SUBTYPE         4
#define DEAUTH_SUBTYPE            0xC
#define ASSOC_RESP_SUBTYPE        1
#define REASSOC_RESP_SUBTYPE      3
#define AUTH_SUBTYPE              11
#define ASSOC_FIXED_LEN           4
#define REASSOC_FIXED_LEN         10
#define AUTH_FIXED_LEN            6
#define BEACON_FIXED_LEN          12
#define MGMT_FRAME_TYPE           0
#define DATA_FRAME_TYPE           2
#define CTRL_FRAME_TYPE           1
// Channels 1-14 are 2.4 GHz; anything above is a 5 GHz channel.
#define FIRST_5GHZ_CHANNEL        15
#define EAPOL_DESCRIPTOR_TYPE     3
#define EAPOL_KEY_DESC_OFFSET     4
#define EAPOL_KEY_DATA_LEN_OFFSET 93
#define EAPOL_KEY_INFO_OFFSET     5
#define EAPOL_MIN_LEN             99
#define RSN_TAG_ID                0x30
#define RSN_MIN_HEADER_LEN        6
#define EAPOL_HEADER_LEN          4
#define EAPOL_BODY_LEN_OFFSET     2
#define EAPOL_REPLAY_OFFSET       9
#define EAPOL_REPLAY_LEN          8
#define EAPOL_NONCE_OFFSET        17
#define EAPOL_NONCE_LEN           32
#define EAPOL_PAIRWISE            0x0008
#define EAPOL_INSTALL             0x0040
#define EAPOL_ACK                 0x0080
#define EAPOL_MIC                 0x0100
#define EAPOL_SECURE              0x0200
#define EAPOL_ERROR_REQUEST       0x0C00
#define EAPOL_ENCRYPTED           0x1000
#define EAPOL_VERSION_MASK        0x0007
#define EAPOL_RSN_DESCRIPTOR      2
#define EAPOL_WPA_DESCRIPTOR      254
#define HANDSHAKE_M1              0
#define HANDSHAKE_M2              1
#define HANDSHAKE_M3              2
#define HANDSHAKE_M4              3
#define HANDSHAKE_MESSAGE_COUNT   4
#define EAPOL_SMK                 0x2000
#define HANDSHAKE_TIMEOUT_MS      10000
#define LLC_HEADER_LEN            8
#define FRAME_FCS_LEN             4
#define FRAME_CONTROL_LEN         2
#define QOS_SUBTYPE               0x08
#define HT_CONTROL_LEN            4
#define FRAME_ORDER_MASK          0x80
#define PMKID_LEN                 16
#define PMKID_KDE_TAG             221
#define PMKID_KDE_SELECTOR_LEN    4
#define METADATA_INTERVAL_MS      5000
#define ASSOC_REQ_SUBTYPE         0
#define REASSOC_REQ_SUBTYPE       2

typedef struct {
  uint8_t bssid[BSSID_LEN];
  uint8_t station[BSSID_LEN];
  uint32_t touched_ms;
  uint32_t message_ms[HANDSHAKE_MESSAGE_COUNT];
  uint8_t replay[HANDSHAKE_MESSAGE_COUNT][EAPOL_REPLAY_LEN];
  uint16_t key_version;
  uint8_t descriptor;
  uint8_t seen_mask;
} sniffer_handshake_session_t;

typedef struct {
  uint8_t bssid[BSSID_LEN];
  bool has_ssid;
  uint32_t beacon_timestamp;
  uint32_t probe_timestamp;
} sniffer_ap_info_t;

static uint8_t *s_pcap_buffer = NULL;
static uint32_t s_buffer_offset = 0;
static uint32_t s_packet_count = 0;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;
static uint32_t s_deauth_count = 0;

// Per-type / per-band tallies, surfaced to the companion app via the stats poll.
// Counted for every frame seen (like s_deauth_count), independent of the save
// filter, so they reflect what is actually on the air.
static uint32_t s_beacon_count = 0;
static uint32_t s_probe_req_count = 0;
static uint32_t s_probe_resp_count = 0;
static uint32_t s_data_count = 0;
static uint32_t s_ctrl_count = 0;
static uint32_t s_mgmt_count = 0;
static uint32_t s_pkts_2ghz = 0;
static uint32_t s_pkts_5ghz = 0;
static uint32_t s_unique_aps = 0;
static uint8_t s_last_channel = 0;
static int8_t s_last_rssi = -127; // RSSI of the last captured frame (-127 = none yet)
static uint32_t s_channel_packets[SPI_WIFI_SPECTRUM_BIN_COUNT];
static int8_t s_channel_peak_rssi[SPI_WIFI_SPECTRUM_BIN_COUNT];
static bool s_is_monitor_mode = false; // packet monitor: counts forever, buffer recycles
static bool s_is_sniffing = false;
static bool s_is_pcap_enabled = false;
static bool s_is_verbose = false;
static wifi_sniffer_type_t s_current_type = WIFI_SNIFFER_TYPE_RAW;
static uint16_t s_snaplen = DEFAULT_SNAPLEN;
static bool s_is_pmkid_captured = false;
static uint8_t s_pmkid_bssid[BSSID_LEN];
static bool s_is_handshake_captured = false;
static uint8_t s_handshake_bssid[BSSID_LEN];

static sniffer_handshake_session_t *s_sessions;
static sniffer_ap_info_t *s_known_aps;

static bool s_is_streaming_sd = false;
static char s_stream_filename[128];
static TaskHandle_t s_stream_task_handle = NULL;
static StackType_t *s_stream_task_stack = NULL;
static StaticTask_t *s_stream_task_tcb = NULL;
static volatile uint32_t s_rb_read_offset = 0;

static bool write_pcap_global_header(void);
static void register_known_ap(const uint8_t *bssid);
static void track_handshake(const uint8_t *bssid, const uint8_t *station, const uint8_t *eapol);
static bool check_pmkid_presence(const uint8_t *payload, int len, const uint8_t *bssid);
static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type);
static void stream_task(void *arg);
static bool save_to_file(const char *path, bool use_sd);

static int spectrum_bin_for_channel(uint8_t channel) {
  if (channel >= 1 && channel <= 14)
    return channel - 1;
  if (channel >= 36 && channel <= 64 && ((channel - 36) % 4) == 0)
    return 14 + (channel - 36) / 4;
  if (channel >= 100 && channel <= 144 && ((channel - 100) % 4) == 0)
    return 22 + (channel - 100) / 4;
  if (channel >= 149 && channel <= 165 && ((channel - 149) % 4) == 0)
    return 34 + (channel - 149) / 4;
  return -1;
}

static esp_err_t prepare_tracking(void) {
  if (s_sessions != NULL)
    return ESP_OK;

  sniffer_handshake_session_t *sessions = NULL;
  sniffer_ap_info_t *known_aps = NULL;
  sessions = heap_caps_calloc(
      MAX_TRACKED_SESSIONS, sizeof(*sessions), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (sessions == NULL) {
    ESP_LOGE(TAG, "Failed to allocate handshake sessions in PSRAM");
    goto cleanup;
  }
  known_aps =
      heap_caps_calloc(MAX_KNOWN_APS, sizeof(*known_aps), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (known_aps == NULL) {
    ESP_LOGE(TAG, "Failed to allocate AP metadata in PSRAM");
    goto cleanup;
  }
  // Retain bounded tables across sessions so late RX callbacks cannot use freed memory.
  s_sessions = sessions;
  s_known_aps = known_aps;
  return ESP_OK;

cleanup:
  free(known_aps);
  free(sessions);
  return ESP_ERR_NO_MEM;
}

static void reset_monitor_counters(void) {
  s_beacon_count = 0;
  s_probe_req_count = 0;
  s_probe_resp_count = 0;
  s_data_count = 0;
  s_ctrl_count = 0;
  s_mgmt_count = 0;
  s_pkts_2ghz = 0;
  s_pkts_5ghz = 0;
  s_unique_aps = 0;
  s_last_channel = 0;
  s_last_rssi = -127;
  memset(s_channel_packets, 0, sizeof(s_channel_packets));
  memset(s_channel_peak_rssi, -127, sizeof(s_channel_peak_rssi));
}

void wifi_sniffer_set_snaplen(uint16_t len) {
  s_snaplen = len;
}

void wifi_sniffer_set_verbose(bool is_verbose) {
  s_is_verbose = is_verbose;
}

bool wifi_sniffer_start(wifi_sniffer_type_t type, uint8_t channel) {
  if (s_is_sniffing) {
    ESP_LOGW(TAG, "Sniffer already running.");
    return false;
  }

  if (prepare_tracking() != ESP_OK)
    return false;
  if (channel == 0 && wifi_service_set_capture_hopping(type == WIFI_SNIFFER_TYPE_EAPOL ||
                                                       type == WIFI_SNIFFER_TYPE_PMKID) != ESP_OK)
    return false;

  const bool is_stream_only = spi_bridge_stream_is_enabled(SPI_ID_WIFI_APP_SNIFFER);
  if (!s_is_monitor_mode && !is_stream_only && s_pcap_buffer == NULL) {
    s_pcap_buffer = (uint8_t *)heap_caps_malloc(SNIFFER_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  }

  s_is_pcap_enabled = !s_is_monitor_mode && !is_stream_only && s_pcap_buffer != NULL;
  if (!s_is_pcap_enabled) {
    if (!is_stream_only && !s_is_monitor_mode) {
      ESP_LOGE(TAG, "Failed to allocate PSRAM buffer.");
      return false;
    }
    s_buffer_offset = 0;
    ESP_LOGD(TAG, "Counter/stream capture without duplicate PCAP buffer");
  }

  if (s_is_pcap_enabled) {
    write_pcap_global_header();
  }
  s_packet_count = 0;
  s_deauth_count = 0;
  s_is_pmkid_captured = false;
  s_is_handshake_captured = false;
  s_current_type = type;
  reset_monitor_counters();

  memset(s_sessions, 0, MAX_TRACKED_SESSIONS * sizeof(*s_sessions));
  memset(s_known_aps, 0, MAX_KNOWN_APS * sizeof(*s_known_aps));

  if (channel > 0) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  } else {
    wifi_service_set_capture_action(wifi_deauther_send_capture_frame);
    wifi_service_start_channel_hopping();
  }

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                                                     WIFI_PROMIS_FILTER_MASK_DATA |
                                                     WIFI_PROMIS_FILTER_MASK_CTRL};
  wifi_service_promiscuous_start(sniffer_callback, &filter);

  s_is_sniffing = true;
  ESP_LOGI(TAG, "Sniffer started (Type: %d, Channel: %d)", type, channel);
  return true;
}

bool wifi_sniffer_start_stream_sd(wifi_sniffer_type_t type, uint8_t channel, const char *filename) {
  if (s_is_sniffing) {
    ESP_LOGW(TAG, "Sniffer already running.");
    return false;
  }

  if (prepare_tracking() != ESP_OK)
    return false;
  if (channel == 0 && wifi_service_set_capture_hopping(type == WIFI_SNIFFER_TYPE_EAPOL ||
                                                       type == WIFI_SNIFFER_TYPE_PMKID) != ESP_OK)
    return false;

  if (!sd_is_mounted()) {
    ESP_LOGE(TAG, "SD Card not mounted.");
    return false;
  }

  if (s_pcap_buffer == NULL) {
    s_pcap_buffer = (uint8_t *)heap_caps_malloc(SNIFFER_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  }
  if (s_pcap_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM buffer.");
    return false;
  }

  strncpy(s_stream_filename, filename, sizeof(s_stream_filename) - 1);

  pcap_global_header_t header;
  header.magic_number = PCAP_MAGIC_NUMBER;
  header.version_major = PCAP_VERSION_MAJOR;
  header.version_minor = PCAP_VERSION_MINOR;
  header.thiszone = 0;
  header.sigfigs = 0;
  header.snaplen = s_snaplen;
  header.network = PCAP_LINK_TYPE_802_11;

  char path[130];
  snprintf(path, sizeof(path), "/%s", filename);

  if (sd_write_binary(path, &header, sizeof(header)) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write header to SD: %s", path);
    return false;
  }

  s_buffer_offset = 0;
  s_rb_read_offset = 0;
  s_packet_count = 0;
  s_current_type = type;
  reset_monitor_counters();

  memset(s_sessions, 0, MAX_TRACKED_SESSIONS * sizeof(*s_sessions));
  memset(s_known_aps, 0, MAX_KNOWN_APS * sizeof(*s_known_aps));

  s_is_pcap_enabled = true;
  s_is_streaming_sd = true;
  s_is_sniffing = true;

  s_stream_task_stack = (StackType_t *)heap_caps_malloc(
      SNIFFER_TASK_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_stream_task_tcb =
      (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (s_stream_task_stack != NULL && s_stream_task_tcb != NULL) {
    s_stream_task_handle = xTaskCreateStaticPinnedToCore(stream_task,
                                                         "sniff_stream",
                                                         SNIFFER_TASK_STACK_SIZE,
                                                         NULL,
                                                         SYS_PRIO_SERVICE_HI,
                                                         s_stream_task_stack,
                                                         s_stream_task_tcb,
                                                         SYS_CORE_MAIN);
  }

  if (s_stream_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create stream task in PSRAM");
    if (s_stream_task_stack != NULL) {
      free(s_stream_task_stack);
      s_stream_task_stack = NULL;
    }
    if (s_stream_task_tcb != NULL) {
      free(s_stream_task_tcb);
      s_stream_task_tcb = NULL;
    }
    s_is_streaming_sd = false;
    s_is_sniffing = false;
    return false;
  }

  if (channel > 0) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  } else {
    wifi_service_set_capture_action(wifi_deauther_send_capture_frame);
    wifi_service_start_channel_hopping();
  }

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                                                     WIFI_PROMIS_FILTER_MASK_DATA};
  wifi_service_promiscuous_start(sniffer_callback, &filter);

  ESP_LOGI(TAG, "Sniffer Stream started (Type: %d, Channel: %d) to %s", type, channel, filename);
  return true;
}

void wifi_sniffer_set_monitor_mode(bool enabled) {
  s_is_monitor_mode = enabled;
  if (!enabled) {
    return;
  }
  // Reset buffer position so monitor mode starts with a clean rolling window.
  s_buffer_offset = sizeof(pcap_global_header_t);
}

void wifi_sniffer_bind_session(uint32_t session_id) {
  s_session_id = session_id;
}

void wifi_sniffer_session_killed(spi_id_t op_id) {
  (void)op_id;
  s_session_id = SPI_SESSION_INVALID_ID;
  wifi_sniffer_stop();
}

void wifi_sniffer_stop(void) {
  s_session_id = SPI_SESSION_INVALID_ID;
  if (!s_is_sniffing)
    return;

  wifi_service_stop_channel_hopping();
  wifi_service_promiscuous_stop();
  wifi_service_set_analysis_band(SPI_WIFI_BAND_DUAL);
  s_is_sniffing = false;
  s_is_streaming_sd = false;

  if (s_stream_task_handle != NULL) {
    vTaskDelay(pdMS_TO_TICKS(SNIFFER_STOP_DELAY_MS));
  }

  if (s_stream_task_stack != NULL) {
    free(s_stream_task_stack);
    s_stream_task_stack = NULL;
  }
  if (s_stream_task_tcb != NULL) {
    free(s_stream_task_tcb);
    s_stream_task_tcb = NULL;
  }

  ESP_LOGI(TAG,
           "Sniffer stopped. Captured %lu packets. Buffer usage: %lu bytes",
           s_packet_count,
           s_buffer_offset);
}

bool wifi_sniffer_save_to_internal_flash(const char *filename) {
  char path[128];
  snprintf(path, sizeof(path), FLASH_STORAGE_WIFI_PCAP "/%s", filename);
  return save_to_file(path, false);
}

bool wifi_sniffer_save_to_sd_card(const char *filename) {
  char path[128];
  snprintf(path, sizeof(path), "/%s", filename);
  return save_to_file(path, true);
}

void wifi_sniffer_free_buffer(void) {
  if (s_pcap_buffer != NULL) {
    free(s_pcap_buffer);
    s_pcap_buffer = NULL;
  }
  s_buffer_offset = 0;
  s_packet_count = 0;
  ESP_LOGI(TAG, "Sniffer buffer freed.");
}

uint32_t wifi_sniffer_get_packet_count(void) {
  return s_packet_count;
}

uint32_t wifi_sniffer_get_deauth_count(void) {
  return s_deauth_count;
}

uint32_t wifi_sniffer_get_buffer_usage(void) {
  return s_buffer_offset;
}

void wifi_sniffer_fill_ext_stats(spi_sniffer_stats_t *out) {
  if (out == NULL)
    return;
  out->beacons = s_beacon_count;
  out->probe_reqs = s_probe_req_count;
  out->probe_resps = s_probe_resp_count;
  out->data_frames = s_data_count;
  out->ctrl_frames = s_ctrl_count;
  out->mgmt_frames = s_mgmt_count;
  out->pkts_2ghz = s_pkts_2ghz;
  out->pkts_5ghz = s_pkts_5ghz;
  out->unique_aps = s_unique_aps;
  out->channel = s_last_channel;
  out->last_rssi = s_last_rssi;
  memcpy(out->channel_packets, s_channel_packets, sizeof(out->channel_packets));
  memcpy(out->channel_peak_rssi, s_channel_peak_rssi, sizeof(out->channel_peak_rssi));
  wifi_service_capture_status_t capture;
  wifi_service_get_capture_status(&capture);
  out->capture_attempts = capture.attempts;
  out->capture_failures = capture.failures;
  out->capture_hold_seconds = capture.hold_seconds;
  out->capture_ap_attempt = capture.ap_attempt;
}

bool wifi_sniffer_pmkid_captured(void) {
  return s_is_pmkid_captured;
}

void wifi_sniffer_clear_pmkid(void) {
  s_is_pmkid_captured = false;
  memset(s_pmkid_bssid, 0, sizeof(s_pmkid_bssid));
}

void wifi_sniffer_get_pmkid_bssid(uint8_t out_bssid[6]) {
  if (out_bssid == NULL)
    return;
  memcpy(out_bssid, s_pmkid_bssid, BSSID_LEN);
}

bool wifi_sniffer_handshake_captured(void) {
  return s_is_handshake_captured;
}

void wifi_sniffer_clear_handshake(void) {
  s_is_handshake_captured = false;
  memset(s_handshake_bssid, 0, sizeof(s_handshake_bssid));
}

void wifi_sniffer_get_handshake_bssid(uint8_t out_bssid[6]) {
  if (out_bssid == NULL)
    return;
  memcpy(out_bssid, s_handshake_bssid, BSSID_LEN);
}

static bool write_pcap_global_header(void) {
  if (s_pcap_buffer == NULL)
    return false;

  pcap_global_header_t header;
  header.magic_number = PCAP_MAGIC_NUMBER;
  header.version_major = PCAP_VERSION_MAJOR;
  header.version_minor = PCAP_VERSION_MINOR;
  header.thiszone = 0;
  header.sigfigs = 0;
  header.snaplen = s_snaplen;
  header.network = PCAP_LINK_TYPE_802_11;

  memcpy(s_pcap_buffer, &header, sizeof(header));
  s_buffer_offset = sizeof(header);
  return true;
}

static void register_known_ap(const uint8_t *bssid) {
  static const uint8_t empty_bssid[BSSID_LEN] = {0};
  int free_idx = -1;
  for (int i = 0; i < MAX_KNOWN_APS; i++) {
    if (memcmp(s_known_aps[i].bssid, bssid, BSSID_LEN) == 0) {
      s_known_aps[i].has_ssid = true;
      return; // already seen: a repeat AP stays, it is not counted again
    }
    if (free_idx < 0 && memcmp(s_known_aps[i].bssid, empty_bssid, BSSID_LEN) == 0)
      free_idx = i;
  }
  if (free_idx < 0)
    return; // table full: stop counting instead of evicting and recounting
  memcpy(s_known_aps[free_idx].bssid, bssid, BSSID_LEN);
  s_known_aps[free_idx].has_ssid = true;
  s_unique_aps++;
}

static bool replay_follows(const uint8_t *previous, const uint8_t *next) {
  uint8_t expected[EAPOL_REPLAY_LEN];
  memcpy(expected, previous, sizeof(expected));
  for (size_t i = sizeof(expected); i > 0; i--) {
    expected[i - 1]++;
    if (expected[i - 1] != 0)
      return memcmp(expected, next, sizeof(expected)) == 0;
  }
  return false;
}

// Handshake pair recognition adapted from pwnagotchi-esp32 capture_model.cpp (hs_complete).
// https://github.com/Gutem/pwnagotchi-esp32
// Reference revision: 976a8afab0745693e0d9693dd1e72bf9e9a7f700
// Copyright (C) 2026 Gutem and contributors.
// The upstream-derived recognition logic is licensed under GPL-3.0-only.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, version 3 of the License. It is distributed WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See LICENSE for the full text.
//
// Modified by HIGH CODE LLC, 2026-11-14: C integration with per-AP/client sessions,
// replay correlation, descriptor/key-version checks and message expiration.
static void track_handshake(const uint8_t *bssid, const uint8_t *station, const uint8_t *eapol) {
  uint16_t key_info =
      ((uint16_t)eapol[EAPOL_KEY_INFO_OFFSET] << 8) | eapol[EAPOL_KEY_INFO_OFFSET + 1];
  if (!(key_info & EAPOL_PAIRWISE) || (key_info & (EAPOL_ERROR_REQUEST | EAPOL_SMK)))
    return;
  static const uint8_t ZERO_NONCE[EAPOL_NONCE_LEN] = {0};
  bool has_nonce = memcmp(eapol + EAPOL_NONCE_OFFSET, ZERO_NONCE, EAPOL_NONCE_LEN) != 0;
  uint8_t descriptor = eapol[EAPOL_KEY_DESC_OFFSET];
  uint8_t message = HANDSHAKE_MESSAGE_COUNT;
  if ((key_info & EAPOL_ACK) && !(key_info & (EAPOL_MIC | EAPOL_INSTALL | EAPOL_SECURE)) &&
      has_nonce)
    message = HANDSHAKE_M1;
  else if ((key_info & EAPOL_MIC) && !(key_info & (EAPOL_ACK | EAPOL_INSTALL | EAPOL_SECURE)) &&
           has_nonce)
    message = HANDSHAKE_M2;
  else if ((key_info & (EAPOL_ACK | EAPOL_MIC | EAPOL_INSTALL)) ==
               (EAPOL_ACK | EAPOL_MIC | EAPOL_INSTALL) &&
           has_nonce)
    message = HANDSHAKE_M3;
  else if ((key_info & EAPOL_MIC) && !(key_info & (EAPOL_ACK | EAPOL_INSTALL)) && !has_nonce &&
           ((key_info & EAPOL_SECURE) || descriptor == EAPOL_WPA_DESCRIPTOR) &&
           eapol[EAPOL_KEY_DESC_OFFSET + EAPOL_KEY_DATA_LEN_OFFSET] == 0 &&
           eapol[EAPOL_KEY_DESC_OFFSET + EAPOL_KEY_DATA_LEN_OFFSET + 1] == 0)
    message = HANDSHAKE_M4;
  if (message == HANDSHAKE_MESSAGE_COUNT)
    return;

  uint32_t now = (uint32_t)(esp_timer_get_time() / US_PER_MS);
  sniffer_handshake_session_t *session = NULL;
  sniffer_handshake_session_t *oldest = &s_sessions[0];
  for (size_t i = 0; i < MAX_TRACKED_SESSIONS; i++) {
    sniffer_handshake_session_t *candidate = &s_sessions[i];
    for (size_t j = 0; j < HANDSHAKE_MESSAGE_COUNT; j++) {
      if ((uint32_t)(now - candidate->message_ms[j]) > HANDSHAKE_TIMEOUT_MS)
        candidate->seen_mask &= (uint8_t)~(1U << j);
    }
    if (memcmp(candidate->bssid, bssid, BSSID_LEN) == 0 &&
        memcmp(candidate->station, station, BSSID_LEN) == 0)
      session = candidate;
    if (candidate->seen_mask == 0 ||
        (uint32_t)(now - candidate->touched_ms) > (uint32_t)(now - oldest->touched_ms))
      oldest = candidate;
  }
  uint16_t key_version = key_info & EAPOL_VERSION_MASK;
  if (session == NULL) {
    session = oldest;
    memset(session, 0, sizeof(*session));
    memcpy(session->bssid, bssid, BSSID_LEN);
    memcpy(session->station, station, BSSID_LEN);
  }
  if (session->key_version != key_version || session->descriptor != descriptor)
    session->seen_mask = 0;
  session->key_version = key_version;
  session->descriptor = descriptor;
  session->touched_ms = now;
  session->message_ms[message] = now;
  memcpy(session->replay[message], eapol + EAPOL_REPLAY_OFFSET, EAPOL_REPLAY_LEN);
  session->seen_mask |= (uint8_t)(1U << message);

  for (size_t first = HANDSHAKE_M1; first < HANDSHAKE_M4; first++) {
    uint8_t mask = (uint8_t)((1U << first) | (1U << (first + 1)));
    if ((session->seen_mask & mask) != mask)
      continue;
    bool is_match =
        first == HANDSHAKE_M2
            ? replay_follows(session->replay[first], session->replay[first + 1])
            : memcmp(session->replay[first], session->replay[first + 1], EAPOL_REPLAY_LEN) == 0;
    if (!is_match)
      continue;
    if (!s_is_handshake_captured) {
      memcpy(s_handshake_bssid, bssid, BSSID_LEN);
      s_is_handshake_captured = true;
    }
    wifi_service_mark_captured(bssid);
    session->seen_mask = 0;
    break;
  }
}

static bool check_pmkid_presence(const uint8_t *payload, int len, const uint8_t *bssid) {
  if (len < EAPOL_MIN_LEN)
    return false;

  int eapol_offset = 8;

  if (len < eapol_offset + EAPOL_KEY_DESC_OFFSET)
    return false;

  const uint8_t *eapol = payload + eapol_offset;
  if (eapol[1] != EAPOL_DESCRIPTOR_TYPE)
    return false;

  int key_desc_offset = eapol_offset + EAPOL_KEY_DESC_OFFSET;
  int key_data_len_offset = key_desc_offset + EAPOL_KEY_DATA_LEN_OFFSET;

  if (len < key_data_len_offset + 2)
    return false;

  uint16_t key_data_len = (payload[key_data_len_offset] << 8) | payload[key_data_len_offset + 1];

  if (key_data_len == 0)
    return false;
  if (len < key_data_len_offset + 2 + key_data_len)
    return false;

  const uint8_t *key_data = payload + key_data_len_offset + 2;
  int offset = 0;

  while (offset + 2 <= key_data_len) {
    uint8_t tag = key_data[offset];
    uint8_t tag_len = key_data[offset + 1];

    if (offset + 2 + tag_len > key_data_len)
      break;

    static const uint8_t PMKID_SELECTOR[PMKID_KDE_SELECTOR_LEN] = {0x00, 0x0F, 0xAC, 0x04};
    if (tag == PMKID_KDE_TAG && tag_len == PMKID_KDE_SELECTOR_LEN + PMKID_LEN &&
        memcmp(key_data + offset + 2, PMKID_SELECTOR, PMKID_KDE_SELECTOR_LEN) == 0) {
      if (!s_is_pmkid_captured) {
        memcpy(s_pmkid_bssid, bssid, BSSID_LEN);
        s_is_pmkid_captured = true;
      }
      wifi_service_mark_captured(bssid);
      return true;
    }
    if (tag == RSN_TAG_ID) {
      int rsn_cursor = 0;
      const uint8_t *rsn_body = key_data + offset + 2;
      int rsn_len = tag_len;

      if (rsn_len < RSN_MIN_HEADER_LEN)
        return false;
      rsn_cursor += RSN_MIN_HEADER_LEN;

      if (rsn_cursor + 2 > rsn_len)
        break;
      uint16_t pairwise_count = rsn_body[rsn_cursor] | (rsn_body[rsn_cursor + 1] << 8);
      rsn_cursor += 2 + (4 * pairwise_count);

      if (rsn_cursor + 2 > rsn_len)
        break;
      uint16_t akm_count = rsn_body[rsn_cursor] | (rsn_body[rsn_cursor + 1] << 8);
      rsn_cursor += 2 + (4 * akm_count);

      if (rsn_cursor + 2 > rsn_len)
        break;
      rsn_cursor += 2;

      if (rsn_cursor + 2 <= rsn_len) {
        uint16_t pmkid_count = rsn_body[rsn_cursor] | (rsn_body[rsn_cursor + 1] << 8);
        if (pmkid_count > 0 && pmkid_count <= (rsn_len - rsn_cursor - 2) / PMKID_LEN) {
          if (!s_is_pmkid_captured) {
            memcpy(s_pmkid_bssid, bssid, BSSID_LEN);
            s_is_pmkid_captured = true;
          }
          wifi_service_mark_captured(bssid);
          return true;
        }
      }
      break;
    }
    offset += 2 + tag_len;
  }

  return false;
}

static void stream_task(void *arg) {
  uint8_t *chunk_buf = (uint8_t *)heap_caps_malloc(SNIFFER_STREAM_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
  if (chunk_buf == NULL) {
    ESP_LOGE(TAG, "Failed to alloc stream buffer in PSRAM");
    vTaskDelete(NULL);
    return;
  }

  char path[130];
  snprintf(path, sizeof(path), "/%s", s_stream_filename);

  while (s_is_streaming_sd && s_is_sniffing) {
    uint32_t write_pos = s_buffer_offset;
    uint32_t read_pos = s_rb_read_offset;

    if (write_pos != read_pos) {
      uint32_t len = 0;
      if (write_pos > read_pos) {
        len = write_pos - read_pos;
      } else {
        len = SNIFFER_BUFFER_SIZE - read_pos;
      }

      if (len > SNIFFER_STREAM_CHUNK_SIZE)
        len = SNIFFER_STREAM_CHUNK_SIZE;

      memcpy(chunk_buf, s_pcap_buffer + read_pos, len);

      if (sd_is_mounted()) {
        sd_append_binary(path, chunk_buf, len);
      }

      s_rb_read_offset = (read_pos + len) % SNIFFER_BUFFER_SIZE;
    } else {
      vTaskDelay(pdMS_TO_TICKS(SNIFFER_STREAM_DELAY_MS));
    }
  }

  free(chunk_buf);
  s_stream_task_handle = NULL;

  vTaskDelete(NULL);
}

static bool should_save_metadata(const uint8_t *bssid, uint8_t subtype) {
  if (subtype == ASSOC_REQ_SUBTYPE || subtype == REASSOC_REQ_SUBTYPE)
    return true;
  if (subtype != BEACON_SUBTYPE && subtype != PROBE_RESP_SUBTYPE)
    return false;
  uint32_t now = (uint32_t)(esp_timer_get_time() / US_PER_MS);
  for (size_t i = 0; i < MAX_KNOWN_APS; i++) {
    if (memcmp(s_known_aps[i].bssid, bssid, BSSID_LEN) != 0)
      continue;
    uint32_t *last = subtype == BEACON_SUBTYPE ? &s_known_aps[i].beacon_timestamp
                                               : &s_known_aps[i].probe_timestamp;
    if (*last != 0 && (uint32_t)(now - *last) < METADATA_INTERVAL_MS)
      return false;
    *last = now;
    return true;
  }
  return false;
}

static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!s_is_sniffing || buf == NULL || type == WIFI_PKT_MISC)
    return;

  const bool is_pcap_ok = s_is_pcap_enabled && s_pcap_buffer != NULL;
  bool pcap_full = is_pcap_ok && !s_is_streaming_sd &&
                   s_buffer_offset >= SNIFFER_BUFFER_SIZE - SNIFFER_BUFFER_MARGIN;
  if (pcap_full && s_is_monitor_mode) {
    // Monitor mode: recycle buffer (counter keeps growing)
    s_buffer_offset = sizeof(pcap_global_header_t);
    pcap_full = false;
  }

  const wifi_promiscuous_pkt_t *ppkt = (const wifi_promiscuous_pkt_t *)buf;
  if (ppkt->rx_ctrl.sig_len < FRAME_CONTROL_LEN + FRAME_FCS_LEN)
    return;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;
  const wifi_frame_control_t *fc = (const wifi_frame_control_t *)&mac_header->frame_control;

  int header_len = MAC_HEADER_BASE_LEN;
  if (fc->to_ds && fc->from_ds)
    header_len = MAC_HEADER_4ADDR_LEN;
  if (fc->type == DATA_FRAME_TYPE && (fc->subtype & QOS_SUBTYPE)) {
    header_len += QOS_HEADER_EXTRA;
    if (ppkt->payload[1] & FRAME_ORDER_MASK)
      header_len += HT_CONTROL_LEN;
  }
  if (fc->type != CTRL_FRAME_TYPE && ppkt->rx_ctrl.sig_len < header_len + FRAME_FCS_LEN)
    return;

  if (fc->type == MGMT_FRAME_TYPE &&
      (fc->subtype == BEACON_SUBTYPE || fc->subtype == PROBE_RESP_SUBTYPE)) {
    register_known_ap(mac_header->addr3);
  }
  if (fc->type == MGMT_FRAME_TYPE && fc->subtype == DEAUTH_SUBTYPE) {
    s_deauth_count++;
    if (s_is_verbose)
      ESP_LOGW(TAG, "Deauth detected!");
  }

  if (fc->type == MGMT_FRAME_TYPE) {
    s_mgmt_count++;
    if (fc->subtype == BEACON_SUBTYPE)
      s_beacon_count++;
    else if (fc->subtype == PROBE_REQ_SUBTYPE)
      s_probe_req_count++;
    else if (fc->subtype == PROBE_RESP_SUBTYPE)
      s_probe_resp_count++;
  } else if (fc->type == DATA_FRAME_TYPE) {
    s_data_count++;
  } else if (fc->type == CTRL_FRAME_TYPE) {
    s_ctrl_count++;
  }

  s_last_channel = ppkt->rx_ctrl.channel;
  s_last_rssi = ppkt->rx_ctrl.rssi;
  int spectrum_bin = spectrum_bin_for_channel(ppkt->rx_ctrl.channel);
  if (spectrum_bin >= 0) {
    s_channel_packets[spectrum_bin]++;
    if (ppkt->rx_ctrl.rssi > s_channel_peak_rssi[spectrum_bin])
      s_channel_peak_rssi[spectrum_bin] = ppkt->rx_ctrl.rssi;
  }
  if (ppkt->rx_ctrl.channel >= FIRST_5GHZ_CHANNEL)
    s_pkts_5ghz++;
  else
    s_pkts_2ghz++;

  if (s_is_verbose)
    ESP_LOGD(TAG,
             "RX type=%u subtype=%u channel=%u rssi=%d",
             (unsigned)fc->type,
             (unsigned)fc->subtype,
             (unsigned)ppkt->rx_ctrl.channel,
             (int)ppkt->rx_ctrl.rssi);

  bool is_save = false;

  switch (s_current_type) {
    case WIFI_SNIFFER_TYPE_BEACON:
      if (fc->type == MGMT_FRAME_TYPE && fc->subtype == BEACON_SUBTYPE)
        is_save = true;
      break;
    case WIFI_SNIFFER_TYPE_PROBE:
      if (fc->type == MGMT_FRAME_TYPE && fc->subtype == PROBE_REQ_SUBTYPE)
        is_save = true;
      break;
    case WIFI_SNIFFER_TYPE_EAPOL:
    case WIFI_SNIFFER_TYPE_PMKID:
      if (fc->type == MGMT_FRAME_TYPE) {
        size_t body_len = ppkt->rx_ctrl.sig_len - header_len - FRAME_FCS_LEN;
        if ((fc->subtype == BEACON_SUBTYPE || fc->subtype == PROBE_RESP_SUBTYPE) &&
            body_len >= BEACON_FIXED_LEN && !fc->to_ds && !fc->from_ds &&
            memcmp(mac_header->addr2, mac_header->addr3, BSSID_LEN) == 0) {
          wifi_service_observe_capture(
              ppkt->rx_ctrl.channel, WIFI_SERVICE_CAPTURE_AP, mac_header->addr3);
          wifi_service_observe_capture_ap(ppkt->rx_ctrl.channel,
                                          mac_header->addr3,
                                          ppkt->payload + header_len + BEACON_FIXED_LEN,
                                          body_len - BEACON_FIXED_LEN);
        } else if ((fc->subtype == ASSOC_REQ_SUBTYPE && body_len >= ASSOC_FIXED_LEN) ||
                   (fc->subtype == REASSOC_REQ_SUBTYPE && body_len >= REASSOC_FIXED_LEN) ||
                   ((fc->subtype == AUTH_SUBTYPE || fc->subtype == ASSOC_RESP_SUBTYPE ||
                     fc->subtype == REASSOC_RESP_SUBTYPE) &&
                    body_len >= AUTH_FIXED_LEN))
          wifi_service_observe_capture(
              ppkt->rx_ctrl.channel, WIFI_SERVICE_CAPTURE_ASSOC, mac_header->addr3);
        is_save = should_save_metadata(mac_header->addr3, fc->subtype);
        break;
      }
      if (fc->type == DATA_FRAME_TYPE && !fc->protected_frame && fc->to_ds != fc->from_ds &&
          ppkt->rx_ctrl.sig_len >= header_len + LLC_HEADER_LEN + EAPOL_HEADER_LEN + FRAME_FCS_LEN) {
        static const uint8_t EAPOL_SNAP[LLC_HEADER_LEN] = {
            0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
        const uint8_t *llc = ppkt->payload + header_len;
        const uint8_t *eapol = llc + LLC_HEADER_LEN;
        uint16_t body_len =
            ((uint16_t)eapol[EAPOL_BODY_LEN_OFFSET] << 8) | eapol[EAPOL_BODY_LEN_OFFSET + 1];
        size_t available = ppkt->rx_ctrl.sig_len - header_len - LLC_HEADER_LEN - FRAME_FCS_LEN;
        if (memcmp(llc, EAPOL_SNAP, LLC_HEADER_LEN) != 0 || body_len > available - EAPOL_HEADER_LEN)
          break;
        is_save = s_current_type == WIFI_SNIFFER_TYPE_EAPOL;
        if (body_len < EAPOL_MIN_LEN - EAPOL_HEADER_LEN || eapol[1] != EAPOL_DESCRIPTOR_TYPE ||
            (eapol[EAPOL_KEY_DESC_OFFSET] != EAPOL_RSN_DESCRIPTOR &&
             eapol[EAPOL_KEY_DESC_OFFSET] != EAPOL_WPA_DESCRIPTOR))
          break;
        size_t key_data_offset = EAPOL_KEY_DESC_OFFSET + EAPOL_KEY_DATA_LEN_OFFSET;
        uint16_t key_data_len =
            ((uint16_t)eapol[key_data_offset] << 8) | eapol[key_data_offset + 1];
        if (key_data_len != body_len - (EAPOL_MIN_LEN - EAPOL_HEADER_LEN))
          break;
        const uint8_t *bssid = fc->from_ds ? mac_header->addr2 : mac_header->addr1;
        wifi_service_observe_capture(ppkt->rx_ctrl.channel, WIFI_SERVICE_CAPTURE_EAPOL, bssid);
        const uint8_t *station = fc->from_ds ? mac_header->addr1 : mac_header->addr2;
        uint16_t key_info =
            ((uint16_t)eapol[EAPOL_KEY_INFO_OFFSET] << 8) | eapol[EAPOL_KEY_INFO_OFFSET + 1];
        bool is_from_ap = (key_info & EAPOL_ACK) != 0;
        if (is_from_ap == (bool)fc->from_ds)
          track_handshake(bssid, station, eapol);
        bool has_pmkid =
            !(key_info & EAPOL_ENCRYPTED) &&
            check_pmkid_presence(llc, LLC_HEADER_LEN + EAPOL_HEADER_LEN + body_len, bssid);
        is_save = s_current_type == WIFI_SNIFFER_TYPE_EAPOL || has_pmkid;
      }
      break;
    case WIFI_SNIFFER_TYPE_RAW:
      is_save = true;
      break;
    default:
      break;
  }

  if (is_save && !s_is_monitor_mode && spi_bridge_stream_is_enabled(SPI_ID_WIFI_APP_SNIFFER)) {
    uint16_t total_len = ppkt->rx_ctrl.sig_len;
    if (total_len > SPI_WIFI_SNIFFER_FRAME_MAX)
      total_len = SPI_WIFI_SNIFFER_FRAME_MAX; // bounded by the P4 reassembly buffer

    uint8_t stream_buf[SPI_MAX_PAYLOAD];
    spi_wifi_sniffer_frame_t *frag = (spi_wifi_sniffer_frame_t *)stream_buf;

    // Split the frame into ordered fragments; the P4 reassembles them. A frame
    // that fits in one transfer is just a single fragment (offset 0, no MORE).
    for (uint16_t off = 0; off < total_len;) {
      uint16_t chunk = total_len - off;
      if (chunk > SPI_WIFI_SNIFFER_FRAG_DATA_MAX)
        chunk = SPI_WIFI_SNIFFER_FRAG_DATA_MAX;

      frag->rssi = ppkt->rx_ctrl.rssi;
      frag->channel = ppkt->rx_ctrl.channel;
      frag->total_len = total_len;
      frag->frag_off = off;
      frag->frag_len = (uint8_t)chunk;
      frag->flags = ((off + chunk) < total_len) ? SPI_WIFI_SNIFFER_FRAG_MORE : 0;
      memcpy(frag->data, ppkt->payload + off, chunk);

      uint8_t buf_len = (uint8_t)(sizeof(spi_wifi_sniffer_frame_t) + chunk);
      bool ok = (s_session_id != SPI_SESSION_INVALID_ID)
                    ? (session_manager_try_emit(s_session_id, stream_buf, buf_len) == ESP_OK)
                    : spi_bridge_stream_push(SPI_ID_WIFI_APP_SNIFFER, stream_buf, buf_len);
      if (!ok)
        break; // backpressure: drop the rest; the P4 discards the partial frame

      off += chunk;
    }
  }

  if (is_save && is_pcap_ok && !pcap_full) {
    pcap_packet_header_t pkt_hdr;
    int64_t now_us = esp_timer_get_time();
    pkt_hdr.ts_sec = (uint32_t)(now_us / US_PER_S);
    pkt_hdr.ts_usec = (uint32_t)(now_us % US_PER_S);
    pkt_hdr.orig_len = ppkt->rx_ctrl.sig_len;

    pkt_hdr.incl_len = (ppkt->rx_ctrl.sig_len > s_snaplen) ? s_snaplen : ppkt->rx_ctrl.sig_len;

    if (s_is_streaming_sd) {
      uint32_t next_write = s_buffer_offset;

      if (next_write + sizeof(pkt_hdr) <= SNIFFER_BUFFER_SIZE) {
        memcpy(s_pcap_buffer + next_write, &pkt_hdr, sizeof(pkt_hdr));
        next_write += sizeof(pkt_hdr);
      } else {
        uint32_t p1 = SNIFFER_BUFFER_SIZE - next_write;
        memcpy(s_pcap_buffer + next_write, &pkt_hdr, p1);
        memcpy(s_pcap_buffer, ((uint8_t *)&pkt_hdr) + p1, sizeof(pkt_hdr) - p1);
        next_write = sizeof(pkt_hdr) - p1;
      }

      if (next_write + pkt_hdr.incl_len <= SNIFFER_BUFFER_SIZE) {
        memcpy(s_pcap_buffer + next_write, ppkt->payload, pkt_hdr.incl_len);
        next_write += pkt_hdr.incl_len;
      } else {
        uint32_t p1 = SNIFFER_BUFFER_SIZE - next_write;
        memcpy(s_pcap_buffer + next_write, ppkt->payload, p1);
        memcpy(s_pcap_buffer, ppkt->payload + p1, pkt_hdr.incl_len - p1);
        next_write = pkt_hdr.incl_len - p1;
      }

      s_buffer_offset = next_write;
    } else {
      if (s_buffer_offset + sizeof(pkt_hdr) + pkt_hdr.incl_len <= SNIFFER_BUFFER_SIZE) {
        memcpy(s_pcap_buffer + s_buffer_offset, &pkt_hdr, sizeof(pkt_hdr));
        s_buffer_offset += sizeof(pkt_hdr);
        memcpy(s_pcap_buffer + s_buffer_offset, ppkt->payload, pkt_hdr.incl_len);
        s_buffer_offset += pkt_hdr.incl_len;
      }
    }
  }

  if (is_save) {
    s_packet_count++;
  }
}

static bool save_to_file(const char *path, bool use_sd) {
  (void)path;
  (void)use_sd;
  // Pcaps are heavy: the C5 never persists them. Frames stream to the P4 over SPI
  // and the P4 writes the .pcap to its SD card. Without a card nothing is saved.
  return false;
}
