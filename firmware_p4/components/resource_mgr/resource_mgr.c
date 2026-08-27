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

#include "resource_mgr.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "RESOURCE_MGR";

#define RES_MAX_LANES  1
#define RES_MAX_GRANTS 16

typedef struct {
  const char *name;
  uint8_t capacity;
  uint8_t reserved; // slots only a privileged owner (system/companion) may take
} lane_desc_t;

typedef struct {
  const char *name;
  bool preemptible; // may a higher-priority owner steal the lane from a holder
  uint8_t lane_count;
  lane_desc_t lanes[RES_MAX_LANES];
} res_desc_t;

// Exclusive radios are one lane, capacity 1, and preemptible: a higher-priority
// owner takes over and the loser is told to stop via on_revoke. BLE is slotted
// and not preemptible: a full lane returns NO_MEM instead of dropping a link.
static const res_desc_t s_desc[RES_COUNT] = {
    [RES_WIFI] = {"wifi", true, 1, {{"", 1, 0}}},
    [RES_BLE] = {"ble", true, 1, {{"", 1, 0}}},
    [RES_SUBGHZ] = {"subghz", true, 1, {{"", 1, 0}}},
    [RES_IR] = {"ir", true, 1, {{"", 1, 0}}},
    [RES_NFC] = {"nfc", true, 1, {{"", 1, 0}}},
    [RES_HID_USB] = {"hid_usb", true, 1, {{"", 1, 0}}},
    [RES_LORA] = {"lora", true, 1, {{"", 1, 0}}},
};

typedef struct {
  bool active;
  uint32_t token;
  res_id_t id;
  uint8_t lane;
  res_owner_kind_t owner_kind;
  void *owner_task;
  uint8_t priority;
  void (*on_revoke)(void *user);
  void *user;
} grant_t;

static grant_t s_grants[RES_MAX_GRANTS];
static uint32_t s_next_token = 1;
static SemaphoreHandle_t s_lock = NULL;

static bool ensure_lock(void) {
  if (s_lock == NULL)
    s_lock = xSemaphoreCreateMutex();
  return s_lock != NULL;
}

static uint8_t default_priority(res_owner_kind_t kind) {
  switch (kind) {
    case RES_OWNER_SYSTEM:
      return 30;
    case RES_OWNER_COMPANION:
      return 20;
    case RES_OWNER_UI:
      return 20;
    case RES_OWNER_APP:
    default:
      return 10;
  }
}

static bool is_privileged(res_owner_kind_t kind) {
  return kind == RES_OWNER_SYSTEM || kind == RES_OWNER_COMPANION;
}

static res_handle_t make_handle(int index, uint32_t token) {
  return ((token & 0xFFFFFFu) << 8) | ((uint32_t)index & 0xFFu);
}

// Callers below hold s_lock.
static uint8_t lane_used(res_id_t id, uint8_t lane) {
  uint8_t used = 0;
  for (int i = 0; i < RES_MAX_GRANTS; i++)
    if (s_grants[i].active && s_grants[i].id == id && s_grants[i].lane == lane)
      used++;
  return used;
}

static int find_free_grant(void) {
  for (int i = 0; i < RES_MAX_GRANTS; i++)
    if (!s_grants[i].active)
      return i;
  return -1;
}

// Lowest-priority active holder of (id, lane) strictly below prio, or -1.
static int find_victim(res_id_t id, uint8_t lane, uint8_t prio) {
  int victim = -1;
  for (int i = 0; i < RES_MAX_GRANTS; i++) {
    grant_t *g = &s_grants[i];
    if (!g->active || g->id != id || g->lane != lane || g->priority >= prio)
      continue;
    if (victim < 0 || g->priority < s_grants[victim].priority)
      victim = i;
  }
  return victim;
}

static void fill_grant(int index, const res_request_t *req, uint8_t prio) {
  grant_t *g = &s_grants[index];
  g->active = true;
  g->token = s_next_token++;
  g->id = req->id;
  g->lane = req->lane;
  g->owner_kind = req->owner_kind;
  g->owner_task = req->owner_task;
  g->priority = prio;
  g->on_revoke = req->on_revoke;
  g->user = req->user;
}

esp_err_t resource_mgr_init(void) {
  return ensure_lock() ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t resource_acquire(const res_request_t *req, res_handle_t *out_handle) {
  if (req == NULL || out_handle == NULL || req->id >= RES_COUNT ||
      req->owner_kind >= RES_OWNER_COUNT)
    return ESP_ERR_INVALID_ARG;
  const res_desc_t *desc = &s_desc[req->id];
  if (req->lane >= desc->lane_count)
    return ESP_ERR_INVALID_ARG;
  if (!ensure_lock())
    return ESP_ERR_NO_MEM;

  *out_handle = RES_HANDLE_NONE;
  const lane_desc_t *lane = &desc->lanes[req->lane];
  uint8_t prio = req->priority ? req->priority : default_priority(req->owner_kind);
  uint8_t cap_for_me = is_privileged(req->owner_kind) ? lane->capacity
                                                      : (uint8_t)(lane->capacity - lane->reserved);

  void (*revoke_cb)(void *) = NULL;
  void *revoke_user = NULL;
  esp_err_t ret;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (lane_used(req->id, req->lane) < cap_for_me) {
    int slot = find_free_grant();
    if (slot < 0) {
      ret = ESP_ERR_NO_MEM;
    } else {
      fill_grant(slot, req, prio);
      *out_handle = make_handle(slot, s_grants[slot].token);
      ret = ESP_OK;
    }
  } else if (desc->preemptible && req->allow_preempt) {
    int victim = find_victim(req->id, req->lane, prio);
    if (victim < 0) {
      ret = ESP_ERR_INVALID_STATE;
    } else {
      revoke_cb = s_grants[victim].on_revoke;
      revoke_user = s_grants[victim].user;
      fill_grant(victim, req, prio); // reuse the slot; new token invalidates the old handle
      *out_handle = make_handle(victim, s_grants[victim].token);
      ret = ESP_OK;
    }
  } else {
    ret = (lane->capacity == 1) ? ESP_ERR_INVALID_STATE : ESP_ERR_NO_MEM;
  }
  xSemaphoreGive(s_lock);

  if (revoke_cb != NULL) {
    ESP_LOGW(TAG, "%s/%s preempted by %s", desc->name, lane->name,
             resource_owner_name(req->owner_kind));
    revoke_cb(revoke_user);
  }
  return ret;
}

esp_err_t resource_release(res_handle_t handle) {
  if (handle == RES_HANDLE_NONE || s_lock == NULL)
    return ESP_OK;
  int index = (int)(handle & 0xFFu);
  uint32_t token = handle >> 8;
  if (index >= RES_MAX_GRANTS)
    return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_grants[index].active && s_grants[index].token == token)
    s_grants[index].active = false;
  xSemaphoreGive(s_lock);
  return ESP_OK;
}

bool resource_available(res_id_t id, uint8_t lane, res_owner_kind_t owner_kind) {
  if (id >= RES_COUNT || lane >= s_desc[id].lane_count || !ensure_lock())
    return false;
  const lane_desc_t *ld = &s_desc[id].lanes[lane];
  uint8_t cap_for_me =
      is_privileged(owner_kind) ? ld->capacity : (uint8_t)(ld->capacity - ld->reserved);
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool avail = lane_used(id, lane) < cap_for_me;
  xSemaphoreGive(s_lock);
  return avail;
}

// Collect the on_revoke callbacks of every grant matching a predicate, mark them
// released, then fire the callbacks outside the lock.
static void release_matching(void *task, bool by_task, res_owner_kind_t kind) {
  if (s_lock == NULL)
    return;
  void (*cbs[RES_MAX_GRANTS])(void *);
  void *users[RES_MAX_GRANTS];
  int n = 0;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < RES_MAX_GRANTS; i++) {
    grant_t *g = &s_grants[i];
    if (!g->active)
      continue;
    bool match = by_task ? (g->owner_task == task) : (g->owner_kind == kind);
    if (!match)
      continue;
    if (g->on_revoke != NULL) {
      cbs[n] = g->on_revoke;
      users[n] = g->user;
      n++;
    }
    g->active = false;
  }
  xSemaphoreGive(s_lock);

  for (int i = 0; i < n; i++)
    cbs[i](users[i]);
}

void resource_release_owner_task(void *owner_task) {
  if (owner_task != NULL)
    release_matching(owner_task, true, RES_OWNER_COUNT);
}

void resource_release_owner_kind(res_owner_kind_t kind) {
  release_matching(NULL, false, kind);
}

int resource_snapshot(res_status_t *out, int max) {
  if (out == NULL || max <= 0 || !ensure_lock())
    return 0;
  int n = 0;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int id = 0; id < RES_COUNT && n < max; id++) {
    const res_desc_t *desc = &s_desc[id];
    for (int l = 0; l < desc->lane_count && n < max; l++) {
      res_status_t *s = &out[n++];
      s->id = (res_id_t)id;
      s->lane = (uint8_t)l;
      s->res_name = desc->name;
      s->lane_name = desc->lanes[l].name;
      s->capacity = desc->lanes[l].capacity;
      s->reserved = desc->lanes[l].reserved;
      s->used = lane_used((res_id_t)id, (uint8_t)l);
      s->holder = RES_OWNER_COUNT;
      for (int i = 0; i < RES_MAX_GRANTS; i++) {
        if (s_grants[i].active && s_grants[i].id == id && s_grants[i].lane == l) {
          s->holder = s_grants[i].owner_kind;
          break;
        }
      }
    }
  }
  xSemaphoreGive(s_lock);
  return n;
}

const char *resource_owner_name(res_owner_kind_t kind) {
  switch (kind) {
    case RES_OWNER_SYSTEM:
      return "system";
    case RES_OWNER_COMPANION:
      return "companion";
    case RES_OWNER_UI:
      return "ui";
    case RES_OWNER_APP:
      return "app";
    default:
      return "none";
  }
}
