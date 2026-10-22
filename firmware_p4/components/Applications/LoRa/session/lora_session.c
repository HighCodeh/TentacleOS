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

#include <stdatomic.h>
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
#include "power_manager.h"
#include "sx1262.h"

#define TX_QUEUE_DEPTH  4
#define MSG_RING        24
#define MT_BROADCAST    0xFFFFFFFFu
#define MC_PUBLIC_CHAN  0
#define SESSION_LOCK_MS 50

static const char *TAG = "LORA_SESSION";

static _Atomic(lora_proto_t) s_proto = LORA_PROTO_NONE;
EXT_RAM_BSS_ATTR static lora_msg_t s_msgs[MSG_RING];
static uint16_t s_head = 0;
static uint16_t s_count = 0;
static uint32_t s_total = 0;
static _Atomic(SemaphoreHandle_t) s_lock;
static _Atomic(SemaphoreHandle_t) s_nodes_lock;
static bool s_no_sleep_held = false;
static atomic_flag s_is_starting = ATOMIC_FLAG_INIT;
EXT_RAM_BSS_ATTR static char s_tx_text[TX_QUEUE_DEPTH][sizeof(((lora_msg_t *)0)->text)];
static uint16_t s_tx_head;
static uint16_t s_tx_count;

static SemaphoreHandle_t ensure_mutex(_Atomic(SemaphoreHandle_t) *slot) {
  SemaphoreHandle_t mutex = atomic_load(slot);
  if (mutex != NULL)
    return mutex;
  SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
  if (candidate == NULL) {
    ESP_LOGE(TAG, "Failed to allocate session mutex");
    return NULL;
  }
  if (!atomic_compare_exchange_strong(slot, &mutex, candidate)) {
    vSemaphoreDelete(candidate);
    return mutex;
  }
  return candidate;
}

static SemaphoreHandle_t ring_mutex(void) {
  return ensure_mutex(&s_lock);
}

esp_err_t lora_session_nodes_acquire(void) {
  SemaphoreHandle_t mutex = ensure_mutex(&s_nodes_lock);
  if (mutex == NULL)
    return ESP_ERR_NO_MEM;
  return xSemaphoreTake(mutex, pdMS_TO_TICKS(SESSION_LOCK_MS)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void lora_session_nodes_release(void) {
  xSemaphoreGive(atomic_load(&s_nodes_lock));
}

static bool ring_lock(void) {
  SemaphoreHandle_t mutex = ring_mutex();
  return mutex != NULL && xSemaphoreTake(mutex, pdMS_TO_TICKS(SESSION_LOCK_MS)) == pdTRUE;
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

static esp_err_t start_protocol(lora_proto_t proto) {
  if (ring_mutex() == NULL)
    return ESP_ERR_NO_MEM;

  if (s_proto != LORA_PROTO_NONE) {
    if (s_proto == proto)
      return ESP_OK;
    ESP_LOGW(TAG, "Radio owned by proto %d; reboot to switch", (int)s_proto);
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_no_sleep_held) {
    esp_err_t pm_err = power_manager_no_sleep_acquire();
    if (pm_err != ESP_OK) {
      ESP_LOGE(TAG, "Cannot hold radio power state: %s", esp_err_to_name(pm_err));
      return pm_err;
    }
    s_no_sleep_held = true;
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
      if (s_no_sleep_held) {
        (void)power_manager_no_sleep_release();
        s_no_sleep_held = false;
      }
      return ESP_ERR_INVALID_ARG;
  }

  if (err == ESP_OK) {
    s_proto = proto;
    ESP_LOGI(TAG, "Started proto %d", (int)proto);
  } else {
    ESP_LOGE(TAG, "Start proto %d failed: %s", (int)proto, esp_err_to_name(err));
    sx1262_deinit();
    if (s_no_sleep_held) {
      (void)power_manager_no_sleep_release();
      s_no_sleep_held = false;
    }
  }
  return err;
}

esp_err_t lora_session_start(lora_proto_t proto) {
  if (proto != LORA_PROTO_MESHTASTIC && proto != LORA_PROTO_MESHCORE)
    return ESP_ERR_INVALID_ARG;
  if (atomic_flag_test_and_set(&s_is_starting))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = start_protocol(proto);
  atomic_flag_clear(&s_is_starting);
  return error;
}

lora_proto_t lora_session_active(void) {
  return s_proto;
}

static esp_err_t send_text(const char *text) {
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

esp_err_t lora_session_send_text(const char *text) {
  if (text == NULL)
    return ESP_ERR_INVALID_ARG;
  size_t length = strnlen(text, sizeof(s_tx_text[0]));
  if (length == 0 || length == sizeof(s_tx_text[0]))
    return ESP_ERR_INVALID_ARG;
  if (s_proto == LORA_PROTO_NONE)
    return ESP_ERR_INVALID_STATE;
  if (!ring_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t error = ESP_ERR_TIMEOUT;
  if (s_tx_count < TX_QUEUE_DEPTH) {
    uint16_t slot = (s_tx_head + s_tx_count) % TX_QUEUE_DEPTH;
    memcpy(s_tx_text[slot], text, length + 1);
    s_tx_count++;
    error = ESP_OK;
  }
  ring_unlock();
  return error;
}

void lora_session_process_tx(lora_proto_t proto) {
  if (proto == LORA_PROTO_NONE || s_proto != proto || !ring_lock())
    return;
  char text[sizeof(s_tx_text[0])];
  bool has_text = s_tx_count != 0;
  if (has_text) {
    memcpy(text, s_tx_text[s_tx_head], sizeof(text));
    s_tx_head = (s_tx_head + 1) % TX_QUEUE_DEPTH;
    s_tx_count--;
  }
  ring_unlock();
  if (has_text) {
    esp_err_t error = send_text(text);
    if (error != ESP_OK)
      ESP_LOGW(TAG, "Queued text failed: %s", esp_err_to_name(error));
  }
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
    if ((uint32_t)(s_total - *io_seq) > s_count)
      *io_seq = oldest;
    while (*io_seq != s_total && copied < max) {
      uint16_t rel = (uint16_t)(*io_seq - oldest);
      out[copied] = s_msgs[(s_head + rel) % MSG_RING];
      copied++;
      (*io_seq)++;
    }
    ring_unlock();
  }
  return copied;
}

esp_err_t lora_session_nodes_snapshot(uint16_t index, lora_node_t *out, uint16_t *out_count) {
  if (out_count == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_count = 0;
  if (out != NULL)
    memset(out, 0, sizeof(*out));
  if (s_proto == LORA_PROTO_MESHTASTIC) {
    mt_node_entry_t entry;
    esp_err_t error = mt_nodedb_snapshot(index, out != NULL ? &entry : NULL, out_count);
    if (error == ESP_OK && out != NULL) {
      const char *name = entry.long_name[0] != '\0' ? entry.long_name : entry.id;
      size_t capacity = name == entry.id ? sizeof(entry.id) : sizeof(entry.long_name);
      size_t length = strnlen(name, capacity);
      if (length >= sizeof(out->name))
        length = sizeof(out->name) - 1;
      memcpy(out->name, name, length);
      out->rssi = entry.rssi;
      out->snr = entry.snr;
    }
    return error;
  }
  if (s_proto == LORA_PROTO_MESHCORE) {
    meshcore_contact_t entry;
    esp_err_t error = meshcore_contacts_snapshot(index, out != NULL ? &entry : NULL, out_count);
    if (error == ESP_OK && out != NULL)
      memcpy(out->name, entry.name, sizeof(out->name) - 1);
    return error;
  }
  return ESP_ERR_INVALID_STATE;
}

uint16_t lora_session_node_count(void) {
  uint16_t count = 0;
  (void)lora_session_nodes_snapshot(0, NULL, &count);
  return count;
}

bool lora_session_node_get(uint16_t idx, lora_node_t *out) {
  uint16_t count;
  return out != NULL && lora_session_nodes_snapshot(idx, out, &count) == ESP_OK;
}

void lora_session_on_rx_text(const char *who, const char *text) {
  ring_push(false, who, text);
}

bool lora_session_app_connected(void) {
  switch (s_proto) {
    case LORA_PROTO_MESHTASTIC:
      return meshtastic_phone_bridge_is_subscribed();
    case LORA_PROTO_MESHCORE:
      return meshcore_phone_bridge_is_subscribed();
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
