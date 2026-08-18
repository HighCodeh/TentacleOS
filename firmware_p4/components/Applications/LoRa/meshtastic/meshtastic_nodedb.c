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

#include "meshtastic_nodedb.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "meshtastic_nvs.h"

static const char *TAG = "MT_NODEDB";

#define MT_NODEDB_NVS_KEY "nodedb"

EXT_RAM_BSS_ATTR static mt_node_entry_t s_nodes[MT_NODEDB_MAX_NODES];
static uint16_t s_count = 0;

static uint64_t dec_varint(const uint8_t *buf, uint16_t max_len, uint16_t *out_used) {
  uint64_t value = 0;
  uint16_t shift = 0;
  uint16_t i = 0;
  while (i < max_len && i < 10) {
    uint8_t byte = buf[i++];
    value |= ((uint64_t)(byte & 0x7F)) << shift;
    if (!(byte & 0x80))
      break;
    shift += 7;
  }
  if (out_used != NULL)
    *out_used = i;
  return value;
}

static int find_slot(uint32_t num) {
  for (int i = 0; i < MT_NODEDB_MAX_NODES; i++) {
    if (s_nodes[i].in_use && s_nodes[i].num == num)
      return i;
  }
  return -1;
}

static int alloc_slot(uint32_t num) {
  int existing = find_slot(num);
  if (existing >= 0)
    return existing;

  int empty = -1;
  for (int i = 0; i < MT_NODEDB_MAX_NODES; i++) {
    if (!s_nodes[i].in_use) {
      empty = i;
      break;
    }
  }
  if (empty >= 0) {
    memset(&s_nodes[empty], 0, sizeof(mt_node_entry_t));
    s_nodes[empty].num = num;
    s_nodes[empty].in_use = true;
    s_count++;
    return empty;
  }

  int victim = -1;
  uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < MT_NODEDB_MAX_NODES; i++) {
    if (s_nodes[i].is_favorite)
      continue;
    if (s_nodes[i].last_heard < oldest) {
      oldest = s_nodes[i].last_heard;
      victim = i;
    }
  }
  if (victim < 0)
    return -1;

  ESP_LOGW(TAG,
           "DB full, evicting 0x%08lX (last_heard=%lu) to allocate 0x%08lX",
           (unsigned long)s_nodes[victim].num,
           (unsigned long)s_nodes[victim].last_heard,
           (unsigned long)num);
  memset(&s_nodes[victim], 0, sizeof(mt_node_entry_t));
  s_nodes[victim].num = num;
  s_nodes[victim].in_use = true;
  return victim;
}

static void parse_user_into(mt_node_entry_t *e, const uint8_t *buf, uint16_t len) {
  uint16_t i = 0;
  while (i < len) {
    uint16_t used = 0;
    uint64_t tag = dec_varint(&buf[i], len - i, &used);
    if (used == 0)
      break;
    i += used;
    uint32_t field = (uint32_t)(tag >> 3);
    uint32_t wire_type = (uint32_t)(tag & 0x07);

    if (wire_type == 2) {
      uint16_t lused = 0;
      uint32_t length = (uint32_t)dec_varint(&buf[i], len - i, &lused);
      i += lused;
      if (i + length > len)
        break;

      uint32_t copy;
      switch (field) {
        case 1:
          copy = length < sizeof(e->id) - 1 ? length : sizeof(e->id) - 1;
          memcpy(e->id, &buf[i], copy);
          e->id[copy] = 0;
          break;
        case 2:
          copy = length < sizeof(e->long_name) - 1 ? length : sizeof(e->long_name) - 1;
          memcpy(e->long_name, &buf[i], copy);
          e->long_name[copy] = 0;
          break;
        case 3:
          copy = length < sizeof(e->short_name) - 1 ? length : sizeof(e->short_name) - 1;
          memcpy(e->short_name, &buf[i], copy);
          e->short_name[copy] = 0;
          break;
        case 8:
          if (length == MT_NODEDB_PUBKEY_LEN) {
            memcpy(e->public_key, &buf[i], MT_NODEDB_PUBKEY_LEN);
            bool all_zero = true;
            for (int k = 0; k < MT_NODEDB_PUBKEY_LEN; k++) {
              if (e->public_key[k] != 0) {
                all_zero = false;
                break;
              }
            }
            e->has_public_key = !all_zero;
          }
          break;
        default:
          break;
      }
      i += length;
    } else if (wire_type == 0) {
      uint16_t vused = 0;
      uint64_t value = dec_varint(&buf[i], len - i, &vused);
      i += vused;
      switch (field) {
        case 5:
          e->hw_model = (uint8_t)value;
          break;
        case 7:
          e->role = (uint8_t)value;
          break;
        default:
          break;
      }
    } else if (wire_type == 5) {
      i += 4;
    } else {
      break;
    }
  }
}

esp_err_t mt_nodedb_init(void) {
  memset(s_nodes, 0, sizeof(s_nodes));
  s_count = 0;

  int n = mt_nvs_get_blob(MT_NODEDB_NVS_KEY, s_nodes, sizeof(s_nodes));
  if (n == (int)sizeof(s_nodes)) {
    for (int i = 0; i < MT_NODEDB_MAX_NODES; i++) {
      if (s_nodes[i].in_use)
        s_count++;
    }
    ESP_LOGI(TAG, "Loaded %u entries from NVS", s_count);
  } else {
    if (n > 0) {
      ESP_LOGW(
          TAG, "NVS blob size mismatch (%d != %u), reinitializing", n, (unsigned)sizeof(s_nodes));
      memset(s_nodes, 0, sizeof(s_nodes));
    } else {
      ESP_LOGI(TAG, "NVS empty - starting clean DB");
    }
  }
  return ESP_OK;
}

bool mt_nodedb_upsert_from_user(uint32_t num,
                                const uint8_t *user_bytes,
                                uint16_t user_len,
                                float snr,
                                int16_t rssi,
                                uint8_t hops_away) {
  int idx = alloc_slot(num);
  if (idx < 0)
    return false;

  mt_node_entry_t *e = &s_nodes[idx];
  bool is_new = (e->long_name[0] == 0);

  parse_user_into(e, user_bytes, user_len);

  if (e->id[0] == 0) {
    snprintf(e->id, sizeof(e->id), "!%08lx", (unsigned long)num);
  }

  e->snr = snr;
  e->rssi = rssi;
  e->hops_away = hops_away;
  e->last_heard = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

  if (is_new) {
    ESP_LOGI(TAG,
             "New peer 0x%08lX '%s' (%s) hw=%u role=%u",
             (unsigned long)num,
             e->long_name,
             e->short_name,
             e->hw_model,
             e->role);
  }
  return true;
}

const mt_node_entry_t *mt_nodedb_get(uint32_t num) {
  int idx = find_slot(num);
  return (idx >= 0) ? &s_nodes[idx] : NULL;
}

const mt_node_entry_t *mt_nodedb_get_by_index(uint16_t idx) {
  uint16_t counted = 0;
  for (int i = 0; i < MT_NODEDB_MAX_NODES; i++) {
    if (!s_nodes[i].in_use)
      continue;
    if (counted == idx)
      return &s_nodes[i];
    counted++;
  }
  return NULL;
}

uint16_t mt_nodedb_count(void) {
  return s_count;
}

bool mt_nodedb_set_favorite(uint32_t num, bool fav) {
  int idx = find_slot(num);
  if (idx < 0)
    return false;
  if (s_nodes[idx].is_favorite == fav)
    return true;
  s_nodes[idx].is_favorite = fav;
  ESP_LOGI(TAG, "favorite 0x%08lX = %s", (unsigned long)num, fav ? "yes" : "no");
  mt_nodedb_save();
  return true;
}

bool mt_nodedb_set_ignored(uint32_t num, bool ign) {
  int idx = find_slot(num);
  if (idx < 0)
    return false;
  if (s_nodes[idx].is_ignored == ign)
    return true;
  s_nodes[idx].is_ignored = ign;
  ESP_LOGI(TAG, "ignored 0x%08lX = %s", (unsigned long)num, ign ? "yes" : "no");
  mt_nodedb_save();
  return true;
}

bool mt_nodedb_toggle_muted(uint32_t num) {
  int idx = find_slot(num);
  if (idx < 0)
    return false;
  s_nodes[idx].is_muted = !s_nodes[idx].is_muted;
  ESP_LOGI(TAG, "muted 0x%08lX = %s", (unsigned long)num, s_nodes[idx].is_muted ? "yes" : "no");
  mt_nodedb_save();
  return true;
}

void mt_nodedb_learn_next_hop(uint32_t num, uint8_t relay_byte) {
  if (relay_byte == 0)
    return;
  int idx = find_slot(num);
  if (idx < 0)
    return;
  if (s_nodes[idx].next_hop != relay_byte) {
    ESP_LOGI(TAG,
             "next_hop 0x%08lX -> 0x%02x (was 0x%02x)",
             (unsigned long)num,
             relay_byte,
             s_nodes[idx].next_hop);
    s_nodes[idx].next_hop = relay_byte;
  }
  s_nodes[idx].next_hop_failures = 0;
}

uint8_t mt_nodedb_get_next_hop(uint32_t num) {
  int idx = find_slot(num);
  if (idx < 0)
    return 0;
  return s_nodes[idx].next_hop;
}

void mt_nodedb_record_next_hop_failure(uint32_t num) {
  int idx = find_slot(num);
  if (idx < 0)
    return;
  s_nodes[idx].next_hop_failures++;
  if (s_nodes[idx].next_hop_failures >= MT_NODEDB_NEXTHOP_MAX_FAILURES) {
    ESP_LOGW(TAG,
             "next_hop 0x%02x for 0x%08lX failed %u times - clearing (fallback to flood)",
             s_nodes[idx].next_hop,
             (unsigned long)num,
             s_nodes[idx].next_hop_failures);
    s_nodes[idx].next_hop = 0;
    s_nodes[idx].next_hop_failures = 0;
  }
}

bool mt_nodedb_remove(uint32_t num) {
  int idx = find_slot(num);
  if (idx < 0)
    return false;
  memset(&s_nodes[idx], 0, sizeof(mt_node_entry_t));
  s_count--;
  ESP_LOGI(TAG, "removed 0x%08lX", (unsigned long)num);
  mt_nodedb_save();
  return true;
}

esp_err_t mt_nodedb_save(void) {
  esp_err_t ret = mt_nvs_set_blob(MT_NODEDB_NVS_KEY, s_nodes, sizeof(s_nodes));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(ret));
  }
  return ret;
}
