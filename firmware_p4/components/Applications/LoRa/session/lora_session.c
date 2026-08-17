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

#include "lora_session.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "meshcore.h"
#include "meshcore_app.h"
#include "meshcore_phone_bridge.h"
#include "meshtastic_app.h"
#include "meshtastic_mesh.h"
#include "meshtastic_nodedb.h"
#include "meshtastic_phone_bridge.h"

#define MSG_RING       24
#define MT_BROADCAST   0xFFFFFFFFu
#define MC_PUBLIC_CHAN 0
#define RING_LOCK_MS   50

static const char *TAG = "LORA_SESSION";

static lora_proto_t s_proto = LORA_PROTO_NONE;
static lora_msg_t s_msgs[MSG_RING];
static uint16_t s_head = 0;
static uint16_t s_count = 0;
static uint32_t s_total = 0;
static SemaphoreHandle_t s_lock = NULL;

static bool ring_lock(void) {
  if (s_lock == NULL)
    s_lock = xSemaphoreCreateMutex();
  if (s_lock == NULL)
    return false;
  return xSemaphoreTake(s_lock, pdMS_TO_TICKS(RING_LOCK_MS)) == pdTRUE;
}

static void ring_unlock(void) {
  if (s_lock != NULL)
    xSemaphoreGive(s_lock);
}

static void ring_push(bool outgoing, const char *who, const char *text) {
  if (!ring_lock())
    return;

  uint16_t slot;
  if (s_count < MSG_RING) {
    slot = (uint16_t)((s_head + s_count) % MSG_RING);
    s_count++;
  } else {
    slot = s_head;
    s_head = (uint16_t)((s_head + 1) % MSG_RING);
  }

  lora_msg_t *m = &s_msgs[slot];
  m->outgoing = outgoing;
  snprintf(m->who, sizeof(m->who), "%s", (who != NULL) ? who : "");
  snprintf(m->text, sizeof(m->text), "%s", (text != NULL) ? text : "");
  s_total++;

  ring_unlock();
}

esp_err_t lora_session_start(lora_proto_t proto) {
  if (s_lock == NULL)
    s_lock = xSemaphoreCreateMutex();

  if (s_proto != LORA_PROTO_NONE) {
    if (s_proto == proto)
      return ESP_OK;
    ESP_LOGW(TAG, "Radio owned by proto %d; reboot to switch", (int)s_proto);
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err;
  switch (proto) {
    case LORA_PROTO_MESHTASTIC:
      err = meshtastic_app_start();
      break;
    case LORA_PROTO_MESHCORE:
      err = meshcore_app_start();
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }

  if (err == ESP_OK) {
    s_proto = proto;
    ESP_LOGI(TAG, "Started proto %d", (int)proto);
  } else {
    ESP_LOGE(TAG, "Start proto %d failed: %s", (int)proto, esp_err_to_name(err));
  }
  return err;
}

lora_proto_t lora_session_active(void) {
  return s_proto;
}

esp_err_t lora_session_send_text(const char *text) {
  if (text == NULL || text[0] == '\0')
    return ESP_ERR_INVALID_ARG;

  esp_err_t err;
  switch (s_proto) {
    case LORA_PROTO_MESHTASTIC:
      err = meshtastic_mesh_send_text(text, MT_BROADCAST);
      break;
    case LORA_PROTO_MESHCORE:
      err = meshcore_send_grp_txt(MC_PUBLIC_CHAN, text);
      break;
    default:
      return ESP_ERR_INVALID_STATE;
  }

  if (err == ESP_OK)
    ring_push(true, "me", text);
  return err;
}

uint16_t lora_session_msg_count(void) {
  uint16_t n = 0;
  if (ring_lock()) {
    n = s_count;
    ring_unlock();
  }
  return n;
}

uint16_t lora_session_msg_since(uint32_t *io_seq, lora_msg_t *out, uint16_t max) {
  if (io_seq == NULL || out == NULL || max == 0)
    return 0;

  uint16_t copied = 0;
  if (ring_lock()) {
    uint32_t oldest = s_total - s_count;
    if (*io_seq < oldest)
      *io_seq = oldest;
    while (*io_seq < s_total && copied < max) {
      uint16_t rel = (uint16_t)(*io_seq - oldest);
      out[copied] = s_msgs[(s_head + rel) % MSG_RING];
      copied++;
      (*io_seq)++;
    }
    ring_unlock();
  }
  return copied;
}

uint16_t lora_session_node_count(void) {
  switch (s_proto) {
    case LORA_PROTO_MESHTASTIC:
      return mt_nodedb_count();
    case LORA_PROTO_MESHCORE:
      return (uint16_t)meshcore_contacts_count();
    default:
      return 0;
  }
}

bool lora_session_node_get(uint16_t idx, lora_node_t *out) {
  if (out == NULL)
    return false;
  memset(out, 0, sizeof(*out));

  if (s_proto == LORA_PROTO_MESHTASTIC) {
    const mt_node_entry_t *n = mt_nodedb_get_by_index(idx);
    if (n == NULL)
      return false;
    snprintf(out->name, sizeof(out->name), "%s", (n->long_name[0] != '\0') ? n->long_name : n->id);
    out->rssi = n->rssi;
    out->snr = n->snr;
    return true;
  }

  if (s_proto == LORA_PROTO_MESHCORE) {
    const meshcore_contact_t *arr = meshcore_contacts_array();
    if (arr == NULL)
      return false;
    uint16_t seen = 0;
    for (uint16_t i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
      if (!arr[i].is_used)
        continue;
      if (seen == idx) {
        snprintf(out->name, sizeof(out->name), "%s", arr[i].name);
        return true;
      }
      seen++;
    }
    return false;
  }

  return false;
}

void lora_session_on_rx_text(const char *who, const char *text) {
  ring_push(false, who, text);
}

bool lora_session_app_connected(void) {
  switch (s_proto) {
    case LORA_PROTO_MESHTASTIC:
      return meshtastic_phone_bridge_is_connected();
    case LORA_PROTO_MESHCORE:
      return meshcore_phone_bridge_is_connected();
    default:
      return false;
  }
}

esp_err_t lora_session_app_connect(void) {
  switch (s_proto) {
    case LORA_PROTO_MESHTASTIC:
      return meshtastic_phone_bridge_ble_start();
    case LORA_PROTO_MESHCORE:
      return meshcore_phone_bridge_ble_start();
    default:
      return ESP_ERR_INVALID_STATE;
  }
}
