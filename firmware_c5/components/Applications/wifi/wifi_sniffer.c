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

static const char *TAG = "WIFI_SNIFFER";

#define SNIFFER_BUFFER_SIZE       (200 * 1024)
#define SNIFFER_BUFFER_MARGIN     2048
#define SNIFFER_STREAM_CHUNK_SIZE 4096
#define SNIFFER_STREAM_DELAY_MS   50
#define SNIFFER_STOP_DELAY_MS     200
#define SNIFFER_TASK_STACK_SIZE   4096
#define SNIFFER_TASK_PRIORITY     SYS_PRIO_SERVICE_HI
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
#define PROBE_TX_COUNT            3
#define PROBE_TX_DELAY_MS         2

typedef struct {
  uint8_t bssid[BSSID_LEN];
  uint8_t station[BSSID_LEN];
  uint32_t m1_timestamp;
  bool has_m1;
} sniffer_handshake_session_t;

typedef struct {
  uint8_t bssid[BSSID_LEN];
  bool has_ssid;
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
static int8_t s_last_rssi = -127;      // RSSI of the last captured frame (-127 = none yet)
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

static sniffer_handshake_session_t s_sessions[MAX_TRACKED_SESSIONS];
static sniffer_ap_info_t s_known_aps[MAX_KNOWN_APS];

static bool s_is_streaming_sd = false;
static char s_stream_filename[128];
static TaskHandle_t s_stream_task_handle = NULL;
static StackType_t *s_stream_task_stack = NULL;
static StaticTask_t *s_stream_task_tcb = NULL;
static volatile uint32_t s_rb_read_offset = 0;

static bool write_pcap_global_header(void);
static void inject_unicast_probe_req(const uint8_t *target_bssid);
static void register_known_ap(const uint8_t *bssid);
static bool is_ap_ssid_known(const uint8_t *bssid);
static void track_handshake(const uint8_t *bssid, const uint8_t *station, uint16_t key_info);
static bool check_pmkid_presence(const uint8_t *payload, int len, const uint8_t *bssid);
static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type);
static void stream_task(void *arg);
static bool save_to_file(const char *path, bool use_sd);

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

  if (s_pcap_buffer == NULL) {
    s_pcap_buffer = (uint8_t *)heap_caps_malloc(SNIFFER_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  }

  const bool is_stream_only = spi_bridge_stream_is_enabled(SPI_ID_WIFI_APP_SNIFFER);
  s_is_pcap_enabled = (s_pcap_buffer != NULL);
  if (!s_is_pcap_enabled) {
    if (!is_stream_only) {
      ESP_LOGE(TAG, "Failed to allocate PSRAM buffer.");
      return false;
    }
    ESP_LOGW(TAG, "PSRAM unavailable; running in streaming-only mode.");
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

  memset(s_sessions, 0, sizeof(s_sessions));
  memset(s_known_aps, 0, sizeof(s_known_aps));

  if (channel > 0) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  } else {
    wifi_service_start_channel_hopping();
  }

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                                                     WIFI_PROMIS_FILTER_MASK_DATA};
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

  memset(s_sessions, 0, sizeof(s_sessions));
  memset(s_known_aps, 0, sizeof(s_known_aps));

  s_is_streaming_sd = true;
  s_is_sniffing = true;

  s_stream_task_stack = (StackType_t *)heap_caps_malloc(
      SNIFFER_TASK_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_stream_task_tcb =
      (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (s_stream_task_stack != NULL && s_stream_task_tcb != NULL) {
    s_stream_task_handle = xTaskCreateStatic(stream_task,
                                             "sniff_stream",
                                             SNIFFER_TASK_STACK_SIZE,
                                             NULL,
                                             SNIFFER_TASK_PRIORITY,
                                             s_stream_task_stack,
                                             s_stream_task_tcb);
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

  wifi_service_promiscuous_stop();
  wifi_service_stop_channel_hopping();
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

static void inject_unicast_probe_req(const uint8_t *target_bssid) {
  uint8_t packet[128];
  uint8_t mac[MAC_LEN];
  esp_fill_random(mac, MAC_LEN);
  mac[0] &= 0xFC;

  int idx = 0;
  packet[idx++] = 0x40;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  memcpy(&packet[idx], target_bssid, BSSID_LEN);
  idx += BSSID_LEN;
  memcpy(&packet[idx], mac, MAC_LEN);
  idx += MAC_LEN;
  memcpy(&packet[idx], target_bssid, BSSID_LEN);
  idx += BSSID_LEN;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96};
  packet[idx++] = 0x01;
  packet[idx++] = sizeof(rates);
  memcpy(&packet[idx], rates, sizeof(rates));
  idx += sizeof(rates);

  for (int i = 0; i < PROBE_TX_COUNT; i++) {
    esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);
    vTaskDelay(pdMS_TO_TICKS(PROBE_TX_DELAY_MS));
  }
  ESP_LOGI(TAG, "Injected Probe Request burst (3x) for SSID reveal");
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

static bool is_ap_ssid_known(const uint8_t *bssid) {
  for (int i = 0; i < MAX_KNOWN_APS; i++) {
    if (memcmp(s_known_aps[i].bssid, bssid, BSSID_LEN) == 0) {
      return s_known_aps[i].has_ssid;
    }
  }
  return false;
}

static void track_handshake(const uint8_t *bssid, const uint8_t *station, uint16_t key_info) {
  bool is_m1 = (key_info & 0x0080) && !(key_info & 0x0100);
  bool is_m2 = !(key_info & 0x0080) && (key_info & 0x0100);

  if (is_m1) {
    for (int i = 0; i < MAX_TRACKED_SESSIONS; i++) {
      if (memcmp(s_sessions[i].bssid, bssid, BSSID_LEN) == 0 &&
          memcmp(s_sessions[i].station, station, BSSID_LEN) == 0) {
        s_sessions[i].has_m1 = true;
        s_sessions[i].m1_timestamp = esp_timer_get_time() / US_PER_MS;
        return;
      }
    }
    int idx = s_packet_count % MAX_TRACKED_SESSIONS;
    memcpy(s_sessions[idx].bssid, bssid, BSSID_LEN);
    memcpy(s_sessions[idx].station, station, BSSID_LEN);
    s_sessions[idx].has_m1 = true;
    s_sessions[idx].m1_timestamp = esp_timer_get_time() / US_PER_MS;
    ESP_LOGI(TAG, "Captured EAPOL M1 (Potential Handshake)");
  } else if (is_m2) {
    for (int i = 0; i < MAX_TRACKED_SESSIONS; i++) {
      if (s_sessions[i].has_m1 && memcmp(s_sessions[i].bssid, bssid, BSSID_LEN) == 0 &&
          memcmp(s_sessions[i].station, station, BSSID_LEN) == 0) {
        ESP_LOGW(TAG, "VALID HANDSHAKE CAPTURED! (M1 + M2)");
        if (!s_is_handshake_captured) {
          memcpy(s_handshake_bssid, bssid, BSSID_LEN);
          s_is_handshake_captured = true;
        }
        s_sessions[i].has_m1 = false;
        return;
      }
    }
    ESP_LOGI(TAG, "Captured EAPOL M2 (Orphaned - Missed M1)");
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
        if (pmkid_count > 0) {
          if (!s_is_pmkid_captured) {
            memcpy(s_pmkid_bssid, bssid, BSSID_LEN);
            s_is_pmkid_captured = true;
          }
          if (!is_ap_ssid_known(bssid)) {
            inject_unicast_probe_req(bssid);
          }
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

static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!s_is_sniffing)
    return;

  const bool is_pcap_ok = (s_pcap_buffer != NULL);
  bool pcap_full = is_pcap_ok && !s_is_streaming_sd &&
                   s_buffer_offset >= SNIFFER_BUFFER_SIZE - SNIFFER_BUFFER_MARGIN;
  if (pcap_full && s_is_monitor_mode) {
    // Monitor mode: recycle buffer (counter keeps growing)
    s_buffer_offset = sizeof(pcap_global_header_t);
    pcap_full = false;
  }

  const wifi_promiscuous_pkt_t *ppkt = (const wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;
  const wifi_frame_control_t *fc = (const wifi_frame_control_t *)&mac_header->frame_control;

  int header_len = MAC_HEADER_BASE_LEN;
  if (fc->to_ds && fc->from_ds)
    header_len = MAC_HEADER_4ADDR_LEN;
  if (fc->subtype & 0x8)
    header_len += QOS_HEADER_EXTRA;

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
  if (ppkt->rx_ctrl.channel >= FIRST_5GHZ_CHANNEL)
    s_pkts_5ghz++;
  else
    s_pkts_2ghz++;

  if (s_is_verbose) {
    if (fc->type == MGMT_FRAME_TYPE && fc->subtype == BEACON_SUBTYPE)
      printf("B");
    else if (fc->type == MGMT_FRAME_TYPE && fc->subtype == PROBE_REQ_SUBTYPE)
      printf("P");
    else if (fc->type == DATA_FRAME_TYPE && fc->subtype == 0)
      printf("D");
    else if (fc->type == CTRL_FRAME_TYPE)
      printf("C");
    else
      printf(".");
    fflush(stdout);
  }

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
      if (fc->type == DATA_FRAME_TYPE) {
        if (ppkt->rx_ctrl.sig_len > header_len + sizeof(wifi_llc_snap_t)) {
          const wifi_llc_snap_t *llc = (const wifi_llc_snap_t *)(ppkt->payload + header_len);
          if (ntohs(llc->type) == WIFI_ETHERTYPE_EAPOL) {
            int eapol_offset = header_len + sizeof(wifi_llc_snap_t);
            if (ppkt->rx_ctrl.sig_len >= eapol_offset + 7) {
              uint8_t *key_info_ptr =
                  (uint8_t *)(ppkt->payload + eapol_offset + EAPOL_KEY_INFO_OFFSET);
              uint16_t key_info = (key_info_ptr[0] << 8) | key_info_ptr[1];

              const uint8_t *bssid = (fc->from_ds) ? mac_header->addr2 : mac_header->addr1;
              const uint8_t *station = (fc->from_ds) ? mac_header->addr1 : mac_header->addr2;

              track_handshake(bssid, station, key_info);
            }

            if (s_current_type == WIFI_SNIFFER_TYPE_EAPOL) {
              is_save = true;
            } else {
              const uint8_t *bssid = (fc->from_ds) ? mac_header->addr2 : mac_header->addr1;
              if (check_pmkid_presence(
                      ppkt->payload + header_len, ppkt->rx_ctrl.sig_len - header_len, bssid)) {
                is_save = true;
                ESP_LOGI(TAG, "PMKID Found!");
              }
            }
          }
        }
      }
      break;
    case WIFI_SNIFFER_TYPE_RAW:
      is_save = true;
      break;
    default:
      break;
  }

  if (is_save && spi_bridge_stream_is_enabled(SPI_ID_WIFI_APP_SNIFFER)) {
    uint8_t stream_buf[SPI_MAX_PAYLOAD];
    spi_wifi_sniffer_frame_t *stream = (spi_wifi_sniffer_frame_t *)stream_buf;
    uint16_t raw_len = ppkt->rx_ctrl.sig_len;
    if (raw_len > SPI_WIFI_SNIFFER_MAX_DATA)
      raw_len = SPI_WIFI_SNIFFER_MAX_DATA;
    stream->rssi = ppkt->rx_ctrl.rssi;
    stream->channel = ppkt->rx_ctrl.channel;
    stream->len = (uint8_t)raw_len;
    memcpy(stream->data, ppkt->payload, raw_len);
    if (s_session_id != SPI_SESSION_INVALID_ID) {
      session_manager_try_emit(s_session_id, stream_buf, (uint8_t)(raw_len + 3));
    } else {
      spi_bridge_stream_push(SPI_ID_WIFI_APP_SNIFFER, stream_buf, (uint8_t)(raw_len + 3));
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
