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

static uint8_t *pcap_buffer = NULL;
static uint32_t buffer_offset = 0;
static uint32_t packet_count = 0;
static bool is_sniffing = false;
static sniff_type_t current_type = SNIFF_TYPE_RAW;
static uint16_t sniffer_snaplen = 65535;

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

static bool check_pmkid_presence(const uint8_t *payload, int len) {
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

static bool is_streaming_sd = false;
static char stream_filename[128];
static TaskHandle_t stream_task_handle = NULL;
static volatile uint32_t rb_read_offset = 0;

static void sniffer_stream_task(void *arg) {
  uint8_t *chunk_buf = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL);
  if (!chunk_buf) {
    ESP_LOGE(TAG, "Failed to alloc stream buffer");
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
            if (current_type == SNIFF_TYPE_EAPOL) {
              save = true;
            } else {
              if (check_pmkid_presence(ppkt->payload + header_len, ppkt->rx_ctrl.sig_len - header_len)) {
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
  current_type = type;

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
  is_streaming_sd = true;
  is_sniffing = true; 

  if (xTaskCreate(sniffer_stream_task, "sniff_stream", 4096, NULL, 5, &stream_task_handle) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create stream task");
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

uint32_t wifi_sniffer_get_buffer_usage(void) {
  return buffer_offset;
}
