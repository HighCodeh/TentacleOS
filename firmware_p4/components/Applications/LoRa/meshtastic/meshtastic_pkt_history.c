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

#include "meshtastic_pkt_history.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MESHTASTIC_PKT_HIST";

#define HOP_HIGHEST_MASK 0x07
#define HOP_OURTX_MASK   0x38
#define HOP_OURTX_SHIFT  3

typedef struct {
  uint32_t sender;
  uint32_t id;
  uint32_t rx_time_ms;
  uint8_t next_hop;
  uint8_t hop_limit_packed;
  uint8_t relayed_by[MT_NUM_RELAYERS];
} mt_pkt_record_t;

static mt_pkt_record_t s_table[MT_PKT_HISTORY_MAX];

static uint32_t now_ms(void) {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static int find_record(uint32_t sender, uint32_t id) {
  for (int i = 0; i < MT_PKT_HISTORY_MAX; i++) {
    if (s_table[i].rx_time_ms != 0 && s_table[i].sender == sender && s_table[i].id == id) {
      return i;
    }
  }
  return -1;
}

static int alloc_slot(void) {
  int oldest_idx = 0;
  uint32_t oldest_time = 0xFFFFFFFF;
  for (int i = 0; i < MT_PKT_HISTORY_MAX; i++) {
    if (s_table[i].rx_time_ms == 0)
      return i;
    if (s_table[i].rx_time_ms < oldest_time) {
      oldest_idx = i;
      oldest_time = s_table[i].rx_time_ms;
    }
  }
  return oldest_idx;
}

static void add_relayer(mt_pkt_record_t *record, uint8_t relayer_id) {
  if (relayer_id == 0)
    return;
  for (int i = 0; i < MT_NUM_RELAYERS; i++) {
    if (record->relayed_by[i] == relayer_id)
      return;
  }
  for (int i = 0; i < MT_NUM_RELAYERS; i++) {
    if (record->relayed_by[i] == 0) {
      record->relayed_by[i] = relayer_id;
      return;
    }
  }
  for (int i = 0; i < MT_NUM_RELAYERS - 1; i++) {
    record->relayed_by[i] = record->relayed_by[i + 1];
  }
  record->relayed_by[MT_NUM_RELAYERS - 1] = relayer_id;
}

void mt_pkt_history_init(void) {
  memset(s_table, 0, sizeof(s_table));
  ESP_LOGI(
      TAG, "Initialized — %d slots, %d relayers per entry", MT_PKT_HISTORY_MAX, MT_NUM_RELAYERS);
}

bool mt_pkt_history_check_add(
    uint32_t sender, uint32_t id, uint8_t hop_limit, uint8_t relayer_id, bool *out_upgraded) {
  if (out_upgraded != NULL)
    *out_upgraded = false;

  int idx = find_record(sender, id);
  if (idx < 0) {
    int slot = alloc_slot();
    memset(&s_table[slot], 0, sizeof(mt_pkt_record_t));
    s_table[slot].sender = sender;
    s_table[slot].id = id;
    s_table[slot].rx_time_ms = now_ms();
    s_table[slot].hop_limit_packed = hop_limit & HOP_HIGHEST_MASK;
    add_relayer(&s_table[slot], relayer_id);
    return false;
  }

  mt_pkt_record_t *record = &s_table[idx];
  uint8_t highest_seen = record->hop_limit_packed & HOP_HIGHEST_MASK;
  if (hop_limit > highest_seen) {
    record->hop_limit_packed =
        (record->hop_limit_packed & ~HOP_HIGHEST_MASK) | (hop_limit & HOP_HIGHEST_MASK);
    if (out_upgraded != NULL)
      *out_upgraded = true;
  }
  record->rx_time_ms = now_ms();
  add_relayer(record, relayer_id);
  return true;
}

bool mt_pkt_history_was_relayer(uint8_t relayer_id, uint32_t sender, uint32_t id) {
  int idx = find_record(sender, id);
  if (idx < 0)
    return false;
  for (int i = 0; i < MT_NUM_RELAYERS; i++) {
    if (s_table[idx].relayed_by[i] == relayer_id)
      return true;
  }
  return false;
}

void mt_pkt_history_remove_relayer(uint8_t relayer_id, uint32_t sender, uint32_t id) {
  int idx = find_record(sender, id);
  if (idx < 0)
    return;
  for (int i = 0; i < MT_NUM_RELAYERS; i++) {
    if (s_table[idx].relayed_by[i] == relayer_id) {
      s_table[idx].relayed_by[i] = 0;
      return;
    }
  }
}

int mt_pkt_history_count(void) {
  int count = 0;
  for (int i = 0; i < MT_PKT_HISTORY_MAX; i++) {
    if (s_table[i].rx_time_ms != 0)
      count++;
  }
  return count;
}
