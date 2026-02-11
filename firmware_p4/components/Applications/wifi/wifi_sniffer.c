// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wifi_sniffer.h"
#include "pcap_serializer.h"
#include "wifi_service.h"
#include "wifi_80211.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage_write.h"
#include "sd_card_write.h"
#include "sd_card_init.h"
#include "storage_mkdir.h"
#include <string.h>
#include <arpa/inet.h> 

static const char *TAG = "WIFI_SNIFFER";

#define SNIFFER_BUFFER_SIZE (200 * 1024) 
#define MAX_TRACKED_SESSIONS 16
#define MAX_KNOWN_APS 32

typedef struct {
  uint8_t bssid[6];
  uint8_t station[6];
  uint32_t m1_timestamp;
  bool has_m1;
} handshake_session_t;

typedef struct {
  uint8_t bssid[6];
  bool has_ssid;
} ap_info_t;

static uint8_t *pcap_buffer = NULL;
static uint32_t buffer_offset = 0;
static uint32_t packet_count = 0;
static uint32_t deauth_count = 0;
static bool is_sniffing = false;
static bool is_verbose = false;
static sniff_type_t current_type = SNIFF_TYPE_RAW;
static uint16_t sniffer_snaplen = 65535;

static handshake_session_t sessions[MAX_TRACKED_SESSIONS];
static ap_info_t known_aps[MAX_KNOWN_APS];

static bool write_pcap_global_header(void) {
  if (pcap_buffer == NULL) return false;

  pcap_global_header_t header;
  header.magic_number = PCAP_MAGIC_NUMBER;
  header.version_major = PCAP_VERSION_MAJOR;
  header.version_minor = PCAP_VERSION_MINOR;
  header.thiszone = 0;
  header.sigfigs = 0;
  header.snaplen = sniffer_snaplen;
  header.network = PCAP_LINK_TYPE_802_11;

  memcpy(pcap_buffer, &header, sizeof(header));
  buffer_offset = sizeof(header);
  return true;
}

static void inject_unicast_probe_req(const uint8_t *target_bssid) {
  uint8_t packet[128];
  uint8_t mac[6];
  esp_fill_random(mac, 6);
  mac[0] &= 0xFC; 

  int idx = 0;
  packet[idx++] = 0x40; // Type: Probe Req
  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  memcpy(&packet[idx], target_bssid, 6); idx += 6;
  memcpy(&packet[idx], mac, 6); idx += 6;
  memcpy(&packet[idx], target_bssid, 6); idx += 6;

  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 

  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96};
  packet[idx++] = 0x01;
  packet[idx++] = sizeof(rates);
  memcpy(&packet[idx], rates, sizeof(rates)); idx += sizeof(rates);

  for (int i = 0; i < 3; i++) {
    esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);
    vTaskDelay(pdMS_TO_TICKS(2)); 
  }
  ESP_LOGI(TAG, "Injected Probe Request burst (3x) for SSID reveal");
}

static void register_known_ap(const uint8_t *bssid) {
  for (int i = 0; i < MAX_KNOWN_APS; i++) {
    if (memcmp(known_aps[i].bssid, bssid, 6) == 0) {
      known_aps[i].has_ssid = true;
      return;
    }
  }
  int idx = packet_count % MAX_KNOWN_APS;
  memcpy(known_aps[idx].bssid, bssid, 6);
  known_aps[idx].has_ssid = true;
}

static bool is_ap_ssid_known(const uint8_t *bssid) {
  for (int i = 0; i < MAX_KNOWN_APS; i++) {
    if (memcmp(known_aps[i].bssid, bssid, 6) == 0) {
      return known_aps[i].has_ssid;
    }
  }
  return false;
}

static void track_handshake(const uint8_t *bssid, const uint8_t *station, uint16_t key_info) {
  bool is_m1 = (key_info & 0x0080) && !(key_info & 0x0100); // Ack=1, MIC=0
  bool is_m2 = !(key_info & 0x0080) && (key_info & 0x0100); // Ack=0, MIC=1

  if (is_m1) {
    for (int i = 0; i < MAX_TRACKED_SESSIONS; i++) {
      if (memcmp(sessions[i].bssid, bssid, 6) == 0 && memcmp(sessions[i].station, station, 6) == 0) {
        sessions[i].has_m1 = true;
        sessions[i].m1_timestamp = esp_timer_get_time() / 1000;
        return;
      }
    }
    int idx = packet_count % MAX_TRACKED_SESSIONS;
    memcpy(sessions[idx].bssid, bssid, 6);
    memcpy(sessions[idx].station, station, 6);
    sessions[idx].has_m1 = true;
    sessions[idx].m1_timestamp = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "Captured EAPOL M1 (Potential Handshake)");
  } 
  else if (is_m2) {
    for (int i = 0; i < MAX_TRACKED_SESSIONS; i++) {
      if (sessions[i].has_m1 && 
        memcmp(sessions[i].bssid, bssid, 6) == 0 && 
        memcmp(sessions[i].station, station, 6) == 0) {

        ESP_LOGW(TAG, "VALID HANDSHAKE CAPTURED! (M1 + M2)");
        sessions[i].has_m1 = false;
        return;
      }
    }
    ESP_LOGI(TAG, "Captured EAPOL M2 (Orphaned - Missed M1)");
  }
}

static bool check_pmkid_presence(const uint8_t *payload, int len, const uint8_t *bssid) {
  if (len < 99) return false;

  int eapol_offset = 8; 

  if (len < eapol_offset + 4) return false;

  const uint8_t *eapol = payload + eapol_offset;
  if (eapol[1] != 3) return false; 

  int key_desc_offset = eapol_offset + 4;
  int key_data_len_offset = key_desc_offset + 93;

  if (len < key_data_len_offset + 2) return false;

  uint16_t key_data_len = (payload[key_data_len_offset] << 8) | payload[key_data_len_offset + 1];

  if (key_data_len == 0) return false;
  if (len < key_data_len_offset + 2 + key_data_len) return false; 

  const uint8_t *key_data = payload + key_data_len_offset + 2;
  int offset = 0;

  while (offset + 2 <= key_data_len) {
    uint8_t tag = key_data[offset];
    uint8_t tag_len = key_data[offset + 1];

    if (offset + 2 + tag_len > key_data_len) break;

    if (tag == 0x30) { 
      int rsn_cursor = 0;
      const uint8_t *rsn_body = key_data + offset + 2;
      int rsn_len = tag_len;

      if (rsn_len < 2 + 4) return false; 
      rsn_cursor += 6;

      if (rsn_cursor + 2 > rsn_len) break;
      uint16_t pairwise_count = rsn_body[rsn_cursor] | (rsn_body[rsn_cursor+1] << 8);
      rsn_cursor += 2 + (4 * pairwise_count);

      if (rsn_cursor + 2 > rsn_len) break;
      uint16_t akm_count = rsn_body[rsn_cursor] | (rsn_body[rsn_cursor+1] << 8);
      rsn_cursor += 2 + (4 * akm_count);

      if (rsn_cursor + 2 > rsn_len) break;
      rsn_cursor += 2;

      if (rsn_cursor + 2 <= rsn_len) {
        uint16_t pmkid_count = rsn_body[rsn_cursor] | (rsn_body[rsn_cursor+1] << 8);
        if (pmkid_count > 0) {
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

void wifi_sniffer_set_snaplen(uint16_t len) {
  sniffer_snaplen = len;
}

void wifi_sniffer_set_verbose(bool verbose) {
    is_verbose = verbose;
}

static bool is_streaming_sd = false;
static char stream_filename[128];
static TaskHandle_t stream_task_handle = NULL;
static StackType_t *stream_task_stack = NULL;
static StaticTask_t *stream_task_tcb = NULL;
static volatile uint32_t rb_read_offset = 0;

static void sniffer_stream_task(void *arg) {
  uint8_t *chunk_buf = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  if (!chunk_buf) {
    ESP_LOGE(TAG, "Failed to alloc stream buffer in PSRAM");
    vTaskDelete(NULL);
    return;
  }

  char path[130];
  snprintf(path, sizeof(path), "/%s", stream_filename);

  while (is_streaming_sd && is_sniffing) {
    uint32_t write_pos = buffer_offset; 
    uint32_t read_pos = rb_read_offset;

    if (write_pos != read_pos) {
      uint32_t len = 0;
      if (write_pos > read_pos) {
        len = write_pos - read_pos;
      } else {
        len = SNIFFER_BUFFER_SIZE - read_pos;
      }

      if (len > 4096) len = 4096;

      memcpy(chunk_buf, pcap_buffer + read_pos, len);

      if (sd_is_mounted()) {
        sd_append_binary(path, chunk_buf, len);
      }

      rb_read_offset = (read_pos + len) % SNIFFER_BUFFER_SIZE;
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }

  free(chunk_buf);
  stream_task_handle = NULL;

  vTaskDelete(NULL);
}

static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!is_sniffing || pcap_buffer == NULL) return;

  if (!is_streaming_sd && buffer_offset >= SNIFFER_BUFFER_SIZE - 2048) {
    return;
  }

  const wifi_promiscuous_pkt_t *ppkt = (const wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;
  const wifi_frame_control_t *fc = (const wifi_frame_control_t *)&mac_header->frame_control;

  int header_len = 24;
  if (fc->to_ds && fc->from_ds) header_len = 30; 
  if (fc->subtype & 0x8) header_len += 2; 

  if (fc->type == 0 && (fc->subtype == 8 || fc->subtype == 5)) {
    register_known_ap(mac_header->addr3); 
  }
  if (fc->type == 0 && fc->subtype == 0xC) {
    deauth_count++;
    if (is_verbose) ESP_LOGW(TAG, "Deauth detected!");
  }

  if (is_verbose) {
      // Simple visualization
      if (fc->type == 0 && fc->subtype == 8) printf("B"); // Beacon
      else if (fc->type == 0 && fc->subtype == 4) printf("P"); // Probe
      else if (fc->type == 2 && fc->subtype == 0) printf("D"); // Data
      else if (fc->type == 1) printf("C"); // Control
      else printf(".");
      fflush(stdout);
  }

  bool save = false;

  switch (current_type) {
    case SNIFF_TYPE_BEACON:
      if (fc->type == 0 && fc->subtype == 8) save = true;
      break;
    case SNIFF_TYPE_PROBE:
      if (fc->type == 0 && fc->subtype == 4) save = true;
      break;
    case SNIFF_TYPE_EAPOL:
    case SNIFF_TYPE_PMKID:
      if (fc->type == 2) {
        if (ppkt->rx_ctrl.sig_len > header_len + sizeof(wifi_llc_snap_t)) {
          const wifi_llc_snap_t *llc = (const wifi_llc_snap_t *)(ppkt->payload + header_len);
          if (ntohs(llc->type) == WIFI_ETHERTYPE_EAPOL) {

            int eapol_offset = header_len + sizeof(wifi_llc_snap_t);
            if (ppkt->rx_ctrl.sig_len >= eapol_offset + 7) {
              uint8_t *key_info_ptr = (uint8_t*)(ppkt->payload + eapol_offset + 5);
              uint16_t key_info = (key_info_ptr[0] << 8) | key_info_ptr[1];

              const uint8_t *bssid = (fc->from_ds) ? mac_header->addr2 : mac_header->addr1;
              const uint8_t *station = (fc->from_ds) ? mac_header->addr1 : mac_header->addr2;

              track_handshake(bssid, station, key_info);
            }

            if (current_type == SNIFF_TYPE_EAPOL) {
              save = true;
            } else {
              const uint8_t *bssid = (fc->from_ds) ? mac_header->addr2 : mac_header->addr1;
              if (check_pmkid_presence(ppkt->payload + header_len, ppkt->rx_ctrl.sig_len - header_len, bssid)) {
                save = true;
                ESP_LOGI(TAG, "PMKID Found!");
              }
            }
          }
        }
      }
      break;
    case SNIFF_TYPE_RAW:
      save = true;
      break;
  }

  if (save) {
    pcap_packet_header_t pkt_hdr;
    int64_t now_us = esp_timer_get_time();
    pkt_hdr.ts_sec = (uint32_t)(now_us / 1000000);
    pkt_hdr.ts_usec = (uint32_t)(now_us % 1000000);
    pkt_hdr.orig_len = ppkt->rx_ctrl.sig_len;

    pkt_hdr.incl_len = (ppkt->rx_ctrl.sig_len > sniffer_snaplen) ? sniffer_snaplen : ppkt->rx_ctrl.sig_len;

    if (is_streaming_sd) {
      uint32_t next_write = buffer_offset;

      if (next_write + sizeof(pkt_hdr) <= SNIFFER_BUFFER_SIZE) {
        memcpy(pcap_buffer + next_write, &pkt_hdr, sizeof(pkt_hdr));
        next_write += sizeof(pkt_hdr);
      } else {
        uint32_t p1 = SNIFFER_BUFFER_SIZE - next_write;
        memcpy(pcap_buffer + next_write, &pkt_hdr, p1);
        memcpy(pcap_buffer, ((uint8_t*)&pkt_hdr) + p1, sizeof(pkt_hdr) - p1);
        next_write = sizeof(pkt_hdr) - p1;
      }

      if (next_write + pkt_hdr.incl_len <= SNIFFER_BUFFER_SIZE) {
        memcpy(pcap_buffer + next_write, ppkt->payload, pkt_hdr.incl_len);
        next_write += pkt_hdr.incl_len;
      } else {
        uint32_t p1 = SNIFFER_BUFFER_SIZE - next_write;
        memcpy(pcap_buffer + next_write, ppkt->payload, p1);
        memcpy(pcap_buffer, ppkt->payload + p1, pkt_hdr.incl_len - p1);
        next_write = pkt_hdr.incl_len - p1;
      }

      buffer_offset = next_write; 
    } else {
      if (buffer_offset + sizeof(pkt_hdr) + pkt_hdr.incl_len <= SNIFFER_BUFFER_SIZE) {
        memcpy(pcap_buffer + buffer_offset, &pkt_hdr, sizeof(pkt_hdr));
        buffer_offset += sizeof(pkt_hdr);
        memcpy(pcap_buffer + buffer_offset, ppkt->payload, pkt_hdr.incl_len);
        buffer_offset += pkt_hdr.incl_len;
        packet_count++;
      }
    }
  }
}

bool wifi_sniffer_start(sniff_type_t type, uint8_t channel) {
  if (is_sniffing) {
    ESP_LOGW(TAG, "Sniffer already running.");
    return false;
  }

  if (pcap_buffer == NULL) {
    pcap_buffer = (uint8_t *)heap_caps_malloc(SNIFFER_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  }

  if (pcap_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM buffer.");
    return false;
  }

  write_pcap_global_header();
  packet_count = 0;
  deauth_count = 0;
  current_type = type;

  memset(sessions, 0, sizeof(sessions));
  memset(known_aps, 0, sizeof(known_aps));

  if (channel > 0) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  } else {
    wifi_service_start_channel_hopping();
  }

  wifi_promiscuous_filter_t filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
  };
  wifi_service_promiscuous_start(sniffer_callback, &filter);

  is_sniffing = true;
  ESP_LOGI(TAG, "Sniffer started (Type: %d, Channel: %d)", type, channel);
  return true;
}

bool wifi_sniffer_start_stream_sd(sniff_type_t type, uint8_t channel, const char *filename) {
  if (is_sniffing) {
    ESP_LOGW(TAG, "Sniffer already running.");
    return false;
  }

  if (!sd_is_mounted()) {
    ESP_LOGE(TAG, "SD Card not mounted.");
    return false;
  }

  if (pcap_buffer == NULL) {
    pcap_buffer = (uint8_t *)heap_caps_malloc(SNIFFER_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  }
  if (!pcap_buffer) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM buffer.");
    return false;
  }

  strncpy(stream_filename, filename, sizeof(stream_filename)-1);

  pcap_global_header_t header;
  header.magic_number = PCAP_MAGIC_NUMBER;
  header.version_major = PCAP_VERSION_MAJOR;
  header.version_minor = PCAP_VERSION_MINOR;
  header.thiszone = 0;
  header.sigfigs = 0;
  header.snaplen = sniffer_snaplen;
  header.network = PCAP_LINK_TYPE_802_11;

  char path[130];
  snprintf(path, sizeof(path), "/%s", filename);

  if (sd_write_binary(path, &header, sizeof(header)) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write header to SD: %s", path);
    return false;
  }

  buffer_offset = 0; 
  rb_read_offset = 0; 
  packet_count = 0;
  current_type = type;

  memset(sessions, 0, sizeof(sessions));
  memset(known_aps, 0, sizeof(known_aps));

  is_streaming_sd = true;
  is_sniffing = true; 

  stream_task_stack = (StackType_t *)heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  stream_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (stream_task_stack && stream_task_tcb) {
    stream_task_handle = xTaskCreateStatic(
      sniffer_stream_task, 
      "sniff_stream", 
      4096, 
      NULL, 
      5, 
      stream_task_stack, 
      stream_task_tcb
    );
  }

  if (stream_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create stream task in PSRAM");
    if (stream_task_stack) { heap_caps_free(stream_task_stack); stream_task_stack = NULL; }
    if (stream_task_tcb) { heap_caps_free(stream_task_tcb); stream_task_tcb = NULL; }
    is_streaming_sd = false;
    is_sniffing = false;
    return false;
  }

  if (channel > 0) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  } else {
    wifi_service_start_channel_hopping();
  }

  wifi_promiscuous_filter_t filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
  };
  wifi_service_promiscuous_start(sniffer_callback, &filter);

  ESP_LOGI(TAG, "Sniffer Stream started (Type: %d, Channel: %d) to %s", type, channel, filename);
  return true;
}

void wifi_sniffer_stop(void) {
  if (!is_sniffing) return;

  wifi_service_promiscuous_stop();
  wifi_service_stop_channel_hopping();
  is_sniffing = false;
  is_streaming_sd = false;

  if (stream_task_handle) {
    vTaskDelay(pdMS_TO_TICKS(200)); 
  } 
  
  if (stream_task_stack) { heap_caps_free(stream_task_stack); stream_task_stack = NULL; }
  if (stream_task_tcb) { heap_caps_free(stream_task_tcb); stream_task_tcb = NULL; }

  ESP_LOGI(TAG, "Sniffer stopped. Captured %lu packets. Buffer usage: %lu bytes", packet_count, buffer_offset);
}

static bool save_to_file(const char *path, bool use_sd) {
  if (pcap_buffer == NULL || buffer_offset == 0) return false;

  if (!use_sd) {
    storage_mkdir_recursive("/assets/storage/wifi/pcap"); 
  }

  esp_err_t err;
  if (use_sd) {
    if (!sd_is_mounted()) return false;
    err = sd_write_binary(path, pcap_buffer, buffer_offset);
  } else {
    err = storage_write_binary(path, pcap_buffer, buffer_offset);
  }

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved PCAP to %s (%lu bytes)", path, buffer_offset);
    return true;
  } else {
    ESP_LOGE(TAG, "Failed to save PCAP: %s", esp_err_to_name(err));
    return false;
  }
}

bool wifi_sniffer_save_to_internal_flash(const char *filename) {
  char path[128];
  snprintf(path, sizeof(path), "/assets/storage/wifi/pcap/%s", filename);
  return save_to_file(path, false);
}

bool wifi_sniffer_save_to_sd_card(const char *filename) {
  char path[128];
  snprintf(path, sizeof(path), "/%s", filename);
  return save_to_file(path, true);
}

void wifi_sniffer_free_buffer(void) {
  if (pcap_buffer) {
    heap_caps_free(pcap_buffer);
    pcap_buffer = NULL;
  }
  buffer_offset = 0;
  packet_count = 0;
  ESP_LOGI(TAG, "Sniffer buffer freed.");
}

uint32_t wifi_sniffer_get_packet_count(void) {
  return packet_count;
}

uint32_t wifi_sniffer_get_deauth_count(void) {
  return deauth_count;
}

uint32_t wifi_sniffer_get_buffer_usage(void) {
  return buffer_offset;
}
