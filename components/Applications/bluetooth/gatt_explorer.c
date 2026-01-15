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


#include "gatt_explorer.h"
#include "bluetooth_service.h"
#include "uuid_lookup.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "cJSON.h"
#include "storage_write.h"
#include <string.h>

static const char *TAG = "GATT_EXPLORER";

typedef struct {
  uint16_t start_handle;
  uint16_t end_handle;
  ble_uuid_any_t uuid;
  char name[64];
} discovered_svc_t;

#define MAX_DISCOVERED_SVCS 16
static discovered_svc_t svcs[MAX_DISCOVERED_SVCS];
static int svc_count = 0;
static int current_svc_idx = 0;

static cJSON *root_json = NULL;
static cJSON *svcs_json = NULL;
static uint16_t conn_handle = 0;
static bool busy = false;

static int explorer_on_disc_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *chr, void *arg);

static void save_and_cleanup(void) {
  if (root_json) {
    char *json_str = cJSON_Print(root_json);
    storage_write_string("/assets/storage/ble/gatt_results.json", json_str);
    free(json_str);
    cJSON_Delete(root_json);
    root_json = NULL;
  }
  busy = false;
  ESP_LOGI(TAG, "Exploration finished and saved.");
}

static int explorer_on_disc_svc(uint16_t conn_h, const struct ble_gatt_error *error,
                                const struct ble_gatt_svc *svc, void *arg) {
  if (error->status == BLE_HS_EDONE) {
    if (svc_count > 0) {
      current_svc_idx = 0;
      ble_gattc_disc_all_chrs(conn_handle, svcs[current_svc_idx].start_handle,
                              svcs[current_svc_idx].end_handle, explorer_on_disc_chr, NULL);
    } else {
      save_and_cleanup();
    }
    return 0;
  }

  if (error->status != 0) {
    ESP_LOGE(TAG, "Service discovery error: %d", error->status);
    save_and_cleanup();
    return 0;
  }

  if (svc_count < MAX_DISCOVERED_SVCS) {
    svcs[svc_count].start_handle = svc->start_handle;
    svcs[svc_count].end_handle = svc->end_handle;
    memcpy(&svcs[svc_count].uuid, &svc->uuid, sizeof(ble_uuid_any_t));
    strncpy(svcs[svc_count].name, uuid_get_name(&svc->uuid.u), 63);

    cJSON *s = cJSON_CreateObject();
    char uuid_str[48];
    ble_uuid_to_str(&svc->uuid.u, uuid_str);
    cJSON_AddStringToObject(s, "uuid", uuid_str);
    cJSON_AddStringToObject(s, "name", svcs[svc_count].name);
    cJSON_AddItemToArray(svcs_json, s);
    cJSON_AddItemToObject(s, "characteristics", cJSON_CreateArray());

    svc_count++;
  }
  return 0;
}

static int explorer_on_disc_chr(uint16_t conn_h, const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *chr, void *arg) {
  if (error->status == BLE_HS_EDONE) {
    current_svc_idx++;
    if (current_svc_idx < svc_count) {
      ble_gattc_disc_all_chrs(conn_handle, svcs[current_svc_idx].start_handle,
                              svcs[current_svc_idx].end_handle, explorer_on_disc_chr, NULL);
    } else {
      save_and_cleanup();
    }
    return 0;
  }

  cJSON *s = cJSON_GetArrayItem(svcs_json, current_svc_idx);
  cJSON *chrs = cJSON_GetObjectItem(s, "characteristics");
  cJSON *c = cJSON_CreateObject();
  char uuid_str[48];
  ble_uuid_to_str(&chr->uuid.u, uuid_str);
  cJSON_AddStringToObject(c, "uuid", uuid_str);
  cJSON_AddStringToObject(c, "name", uuid_get_name(&chr->uuid.u));
  cJSON_AddNumberToObject(c, "handle", chr->val_handle);
  cJSON_AddNumberToObject(c, "properties", chr->properties);
  cJSON_AddItemToArray(chrs, c);

  return 0;
}

static int explorer_gap_event(struct ble_gap_event *event, void *arg) {
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    if (event->connect.status == 0) {
      conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "Connected. Discovering services...");
      ble_gattc_disc_all_svcs(conn_handle, explorer_on_disc_svc, NULL);
    } else {
      ESP_LOGE(TAG, "Connection failed: %d", event->connect.status);
      busy = false;
    }
  } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    ESP_LOGI(TAG, "Disconnected.");
    busy = false;
  }
  return 0;
}

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type) {
  if (busy) return false;
  busy = true;
  svc_count = 0;

  root_json = cJSON_CreateObject();
  char addr_str[18];
  snprintf(addr_str, 18, "%02x:%02x:%02x:%02x:%02x:%02x", 
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  cJSON_AddStringToObject(root_json, "target", addr_str);
  svcs_json = cJSON_AddArrayToObject(root_json, "services");

  if (bluetooth_service_connect(addr, addr_type, explorer_gap_event) != ESP_OK) {
    cJSON_Delete(root_json);
    root_json = NULL;
    busy = false;
    return false;
  }
  return true;
}

bool gatt_explorer_is_busy(void) {
  return busy;
}
