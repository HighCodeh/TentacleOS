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



#include "bluetooth_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "nvs_flash.h"
#include "assert.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_stop.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/dis/ble_svc_dis.h"

#include "storage_write.h"
#include "storage_assets.h"
#include "cJSON.h"

#define BLE_ANNOUNCE_CONFIG_FILE "config/bluetooth/ble_announce.conf"
#define BLE_ANNOUNCE_CONFIG_PATH "/assets/" BLE_ANNOUNCE_CONFIG_FILE
#define BLE_SPAM_LIST_FILE "config/bluetooth/beacon_list.conf"
#define BLE_SPAM_LIST_PATH "/assets/" BLE_SPAM_LIST_FILE

static const char *TAG = "BLE_SERVICE";

static uint8_t own_addr_type;
static bool ble_initialized = false;
static SemaphoreHandle_t ble_hs_synced_sem = NULL; 

static void bluetooth_service_on_reset(int reason);
static void bluetooth_service_on_sync(void);
static void bluetooth_service_host_task(void *param);
static int bluetooth_service_gap_event(struct ble_gap_event *event, void *arg);
static void bluetooth_load_announce_config(char* name, uint8_t* max_conn);


static int bluetooth_service_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(TAG, "Dispositivo conectado! status=%d conn_handle=%d",
               event->connect.status, event->connect.conn_handle);
      if (event->connect.status != 0) {
        bluetooth_service_start_advertising();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "Dispositivo desconectado! reason=%d", event->disconnect.reason);
      bluetooth_service_start_advertising();
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(TAG, "Anúncio concluído!");
      bluetooth_service_start_advertising();
      break;
    default:
      break;
  }
  return 0;
}

esp_err_t bluetooth_service_start_advertising(void) {
  if (!ble_initialized) {
    ESP_LOGE(TAG, "BLE não inicializado. Não é possível anunciar.");
    return ESP_FAIL;
  }

  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  int rc;

  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(BLE_SVC_DIS_UUID16) };
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  const char* device_name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Erro ao configurar campos de anúncio; rc=%d", rc);
    return ESP_FAIL;
  }

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bluetooth_service_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Erro ao iniciar anúncio; rc=%d", rc);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Anúncio padrão iniciado com sucesso");
  return ESP_OK;
}

esp_err_t bluetooth_service_stop_advertising(void) {
  if (!ble_initialized) {
    ESP_LOGE(TAG, "BLE não inicializado.");
    return ESP_FAIL;
  }
  if (ble_gap_adv_active()) {
    return ble_gap_adv_stop();
  }
  return ESP_OK;
}

void bluetooth_service_on_reset(int reason) {
  ESP_LOGE(TAG, "Resetando estado; reason=%d", reason);
}

void bluetooth_service_on_sync(void) {
  ESP_LOGI(TAG, "BLE sincronizado");
  int rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Erro ao inferir tipo de endereço: %d", rc);
    return;
  }
  if (ble_hs_synced_sem != NULL) {
    xSemaphoreGive(ble_hs_synced_sem);
  }
}

void bluetooth_service_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Iniciada");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t bluetooth_service_init(void) {
  if (ble_initialized) {
    ESP_LOGW(TAG, "BLE já inicializado");
    return ESP_OK;
  }

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao inicializar NimBLE: %s", esp_err_to_name(ret));
    return ret;
  }

  ble_hs_cfg.reset_cb = bluetooth_service_on_reset;
  ble_hs_cfg.sync_cb = bluetooth_service_on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  char device_name[32] = "Darth Maul";
  uint8_t max_conn = 4; 
  bluetooth_load_announce_config(device_name, &max_conn);

  ret = ble_svc_gap_device_name_set(device_name);
  assert(ret == 0);

  ble_hs_synced_sem = xSemaphoreCreateBinary();
  if (ble_hs_synced_sem == NULL) {
    ESP_LOGE(TAG, "Falha ao criar semáforo");
    return ESP_ERR_NO_MEM;
  }

  nimble_port_freertos_init(bluetooth_service_host_task);

  ESP_LOGI(TAG, "Aguardando sincronização do Host BLE...");
  if (xSemaphoreTake(ble_hs_synced_sem, pdMS_TO_TICKS(10000)) == pdFALSE) {
    ESP_LOGE(TAG, "Timeout na sincronização do Host BLE");
    bluetooth_service_stop();
    return ESP_ERR_TIMEOUT;
  }

  ble_initialized = true;
  ESP_LOGI(TAG, "Inicialização do BLE concluída com sucesso");
  return ESP_OK;
}

esp_err_t bluetooth_service_stop(void) {
  if (!ble_initialized) {
    ESP_LOGW(TAG, "BLE não inicializado ou já parado");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Parando serviço BLE");
  int rc = nimble_port_stop();
  if (rc != 0) {
    ESP_LOGE(TAG, "Falha ao parar a porta NimBLE: %d", rc);
  }

  nimble_port_deinit();

  if (ble_hs_synced_sem != NULL) {
    vSemaphoreDelete(ble_hs_synced_sem);
    ble_hs_synced_sem = NULL;
  }

  ble_initialized = false;
  ESP_LOGI(TAG, "Serviço BLE parado");
  return ESP_OK;
}

esp_err_t bluetooth_service_set_max_power(void) {
  esp_err_t err;

  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao definir potência TX para ADV: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Potência TX para ADV definida para MAX (P9)");
  }

  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao definir potência TX Default: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Potência TX Default definida para MAX (P9)");
  }

  return ESP_OK; 
}

uint8_t bluetooth_service_get_own_addr_type(void) {
  return own_addr_type;
}


esp_err_t bluetooth_save_announce_config(const char *name, uint8_t max_conn) {
  if (name == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return ESP_ERR_NO_MEM;
  }

  cJSON_AddStringToObject(root, "ssid", name);
  cJSON_AddNumberToObject(root, "max_conn", max_conn);

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = storage_write_string(BLE_ANNOUNCE_CONFIG_PATH, json_string);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Configuração BLE salva com sucesso em: %s", BLE_ANNOUNCE_CONFIG_PATH);
  } else {
    ESP_LOGE(TAG, "Erro ao gravar arquivo de configuração BLE: %s", esp_err_to_name(err));
  }

  free(json_string);
  cJSON_Delete(root);
  return err;
}

static void bluetooth_load_announce_config(char* name, uint8_t* max_conn) {
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char*)storage_assets_load_file(BLE_ANNOUNCE_CONFIG_FILE, &size);

  if (buffer != NULL) {
    cJSON *root = cJSON_Parse(buffer);
    if (root) {
      cJSON *j_name = cJSON_GetObjectItem(root, "ssid");
      cJSON *j_conn = cJSON_GetObjectItem(root, "max_conn");

      if (cJSON_IsString(j_name) && (strlen(j_name->valuestring) > 0)) {
        strncpy(name, j_name->valuestring, 31);
        name[31] = '\0';
      }
      if (cJSON_IsNumber(j_conn)){
        *max_conn = (uint8_t)j_conn->valueint;
      }

      cJSON_Delete(root);
      ESP_LOGI(TAG, "Configurações BLE carregadas com sucesso.");
    }
    free(buffer);
  } else {
    ESP_LOGW(TAG, "Arquivo de config BLE não encontrado. Usando padrões.");
  }
}

esp_err_t bluetooth_load_spam_list(char ***list, size_t *count) {
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char*)storage_assets_load_file(BLE_SPAM_LIST_FILE, &size);

  if (buffer == NULL) {
    ESP_LOGE(TAG, "Falha ao carregar lista de spam.");
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(buffer);
  if (!root) {
    free(buffer);
    return ESP_FAIL;
  }

  cJSON *spam_array = cJSON_GetObjectItem(root, "spam_ble_announce");
  if (!cJSON_IsArray(spam_array)) {
    cJSON_Delete(root);
    free(buffer);
    return ESP_FAIL;
  }

  int array_size = cJSON_GetArraySize(spam_array);
  *list = malloc(array_size * sizeof(char*));
  if (*list == NULL) {
    cJSON_Delete(root);
    free(buffer);
    return ESP_ERR_NO_MEM;
  }

  *count = 0;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, spam_array) {
    if (cJSON_IsString(item)) {
      (*list)[*count] = strdup(item->valuestring);
      (*count)++;
    }
  }

  cJSON_Delete(root);
  free(buffer);
  return ESP_OK;
}

esp_err_t bluetooth_save_spam_list(const char * const *list, size_t count) {
  cJSON *root = cJSON_CreateObject();
  cJSON *array = cJSON_CreateStringArray(list, count);

  cJSON_AddItemToObject(root, "spam_ble_announce", array);

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = storage_write_string(BLE_SPAM_LIST_PATH, json_string);

  free(json_string);
  cJSON_Delete(root);
  return err;
}

void bluetooth_free_spam_list(char **list, size_t count) {
  if (list != NULL) {
    for (size_t i = 0; i < count; i++) {
      free(list[i]);
    }
    free(list);
  }
}
