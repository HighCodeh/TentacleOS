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

#include "mt_mod_text.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "unishox2.h"

#include "lora_session.h"
#include "meshtastic_nodedb.h"

static const char *TAG = "MT_MOD_TEXT";

#define MT_TEXT_DEDUP_RING 50
#define MT_TEXT_MAX_LEN    220
#define MT_WHO_NAME_LEN    24

static uint32_t s_seen_ids[MT_TEXT_DEDUP_RING];
static uint8_t s_seen_idx = 0;
static uint32_t s_node_num = 0;

static bool has_seen_recently(uint32_t id) {
  for (int i = 0; i < MT_TEXT_DEDUP_RING; i++) {
    if (s_seen_ids[i] == id)
      return true;
  }
  return false;
}

static void mark_seen(uint32_t id) {
  s_seen_ids[s_seen_idx] = id;
  s_seen_idx = (s_seen_idx + 1) % MT_TEXT_DEDUP_RING;
}

void mt_mod_text_init(uint32_t node_num) {
  s_node_num = node_num;
  memset(s_seen_ids, 0, sizeof(s_seen_ids));
  s_seen_idx = 0;
  ESP_LOGI(TAG, "Initialized - dedup ring=%d", MT_TEXT_DEDUP_RING);
}

void mt_mod_text_on_received(const mt_packet_meta_t *meta,
                             const uint8_t *payload,
                             uint16_t len,
                             bool is_compressed) {
  if (has_seen_recently(meta->id)) {
    ESP_LOGD(TAG, "Duplicate text id=0x%08lX, dropping", (unsigned long)meta->id);
    return;
  }
  mark_seen(meta->id);

  char text[MT_TEXT_MAX_LEN];
  uint16_t text_len = 0;

  if (is_compressed) {
    int out_len = unishox2_decompress_simple((const char *)payload, (int)len, text);
    if (out_len <= 0 || out_len >= (int)sizeof(text)) {
      ESP_LOGW(TAG,
               "Unishox2 decompress failed from 0x%08lX (in=%u out=%d)",
               (unsigned long)meta->from,
               len,
               out_len);
      return;
    }
    text[out_len] = 0;
    text_len = (uint16_t)out_len;
    ESP_LOGI(TAG,
             "Text[uncompressed %u->%u] from 0x%08lX ch=%u RSSI=%d SNR=%d: \"%s\"",
             len,
             text_len,
             (unsigned long)meta->from,
             meta->channel,
             meta->rssi_dbm,
             meta->snr_db,
             text);
  } else {
    uint16_t copy_len = len;
    if (copy_len >= sizeof(text))
      copy_len = sizeof(text) - 1;
    memcpy(text, payload, copy_len);
    text[copy_len] = 0;
    text_len = copy_len;
    ESP_LOGI(TAG,
             "Text from 0x%08lX ch=%u RSSI=%d SNR=%d: \"%s\"",
             (unsigned long)meta->from,
             meta->channel,
             meta->rssi_dbm,
             meta->snr_db,
             text);
  }
  (void)text_len;

  char who[MT_WHO_NAME_LEN];
  const mt_node_entry_t *node = mt_nodedb_get(meta->from);
  if (node != NULL && node->short_name[0] != '\0')
    snprintf(who, sizeof(who), "%s", node->short_name);
  else
    snprintf(who, sizeof(who), "!%08lx", (unsigned long)meta->from);
  lora_session_on_rx_text(who, text);
}
