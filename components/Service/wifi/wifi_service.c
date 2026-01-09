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


#include "wifi_service.h"
#include "esp_err.h"
#include "led_control.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage_write.h"
#include "storage_assets.h"
#include "cJSON.h"
#include "lwip/inet.h"
#include "esp_heap_caps.h"
// #include "virtual_display_client.h" // Adicionar este include

#define WIFI_AP_CONFIG_FILE "config/wifi/wifi_ap.conf"
#define WIFI_AP_CONFIG_PATH "/assets/" WIFI_AP_CONFIG_FILE

static wifi_ap_record_t stored_aps[WIFI_SCAN_LIST_SIZE];
static uint16_t stored_ap_count = 0;
static SemaphoreHandle_t wifi_mutex = NULL;
static void wifi_load_ap_config(char* ssid, char* passwd, uint8_t* max_conn, char* ip_addr);

static TaskHandle_t channel_hopper_task_handle = NULL;
static StackType_t *hopper_task_stack = NULL;
static StaticTask_t *hopper_task_tcb = NULL;
#define HOPPER_STACK_SIZE 4096

static const char *TAG = "wifi_service";

static void channel_hopper_task(void *pvParameters) {
  uint8_t channel = 1;
  while (1) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    channel++;
    if (channel > 13) {
      channel = 1;
    }
    vTaskDelay(pdMS_TO_TICKS(250)); 
  }
}

void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter) {
  esp_err_t err;

  if (filter) {
    err = esp_wifi_set_promiscuous_filter(filter);
    if (err != ESP_OK) ESP_LOGE(TAG, "Failed to set promiscuous filter: %s", esp_err_to_name(err));
  }

  if (cb) {
    err = esp_wifi_set_promiscuous_rx_cb(cb);
    if (err != ESP_OK) ESP_LOGE(TAG, "Failed to set promiscuous callback: %s", esp_err_to_name(err));
  }

  err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable promiscuous mode: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Promiscuous mode enabled.");
  }
}

void wifi_service_promiscuous_stop(void) {
  esp_err_t err = esp_wifi_set_promiscuous(false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to disable promiscuous mode: %s", esp_err_to_name(err));
  }

  esp_wifi_set_promiscuous_rx_cb(NULL);
  ESP_LOGI(TAG, "Promiscuous mode disabled.");
}

void wifi_service_start_channel_hopping(void) {
  if (channel_hopper_task_handle != NULL) {
    ESP_LOGW(TAG, "Channel hopping already running.");
    return;
  }

  if (hopper_task_stack == NULL) {
    hopper_task_stack = (StackType_t *)heap_caps_malloc(HOPPER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  }
  if (hopper_task_tcb == NULL) {
    hopper_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
  }

  if (hopper_task_stack == NULL || hopper_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate channel hopper task memory in PSRAM! Falling back to internal RAM.");

    if (hopper_task_stack) { heap_caps_free(hopper_task_stack); hopper_task_stack = NULL; }
    if (hopper_task_tcb) { heap_caps_free(hopper_task_tcb); hopper_task_tcb = NULL; }

    xTaskCreate(channel_hopper_task, "chan_hopper_srv", 4096, NULL, 5, &channel_hopper_task_handle);
  } else {
    channel_hopper_task_handle = xTaskCreateStatic(
      channel_hopper_task,
      "chan_hopper_srv",
      HOPPER_STACK_SIZE,
      NULL,
      5,
      hopper_task_stack,
      hopper_task_tcb
    );
  }

  if (channel_hopper_task_handle) {
    ESP_LOGI(TAG, "Channel hopping started.");
  } else {
    ESP_LOGE(TAG, "Failed to start channel hopping task.");
  }
}

void wifi_service_stop_channel_hopping(void) {
  if (channel_hopper_task_handle) {
    vTaskDelete(channel_hopper_task_handle);
    channel_hopper_task_handle = NULL;
  }

  if (hopper_task_stack) {
    heap_caps_free(hopper_task_stack);
    hopper_task_stack = NULL;
  }
  if (hopper_task_tcb) {
    heap_caps_free(hopper_task_tcb);
    hopper_task_tcb = NULL;
  }
  ESP_LOGI(TAG, "Channel hopping stopped.");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "Estação conectada ao AP, MAC: " MACSTR, MAC2STR(event->mac));
    led_blink_green();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    led_blink_red();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
    ESP_LOGI(TAG, "IP atribuído a estação conectada ao AP");
    led_blink_green();
  }
}

void wifi_change_to_hotspot(const char *new_ssid) {
  ESP_LOGI(TAG, "Tentando mudar o SSID do AP para: %s (aberto)", new_ssid);

  esp_err_t err = esp_wifi_stop();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao parar o Wi-Fi: %s", esp_err_to_name(err));
    led_blink_red(); 
    return;
  }

  wifi_config_t new_ap_config = {
    .ap = {
      .ssid_len = strlen(new_ssid),
      .channel = 1, 
      .authmode = WIFI_AUTH_OPEN,
      .max_connection = 4, 
    },
  };

  esp_wifi_set_mode(WIFI_MODE_AP);

  strncpy((char *)new_ap_config.ap.ssid, new_ssid, sizeof(new_ap_config.ap.ssid) - 1);
  new_ap_config.ap.ssid[sizeof(new_ap_config.ap.ssid) - 1] = '\0'; 

  new_ap_config.ap.password[0] = '\0';

  err = esp_wifi_set_config(WIFI_IF_AP, &new_ap_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao definir a nova configuração do AP Wi-Fi: %s", esp_err_to_name(err));
    led_blink_red(); 
    return;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao iniciar o Wi-Fi com a nova configuração: %s", esp_err_to_name(err));
    led_blink_red(); 
    return;
  }

  ESP_LOGI(TAG, "SSID do AP Wi-Fi alterado com sucesso para: %s (aberto)", new_ssid);
  led_blink_green();
}

void wifi_init(void) {
  esp_err_t err;

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }

  esp_netif_t *netif_ap = esp_netif_create_default_wifi_ap();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Driver Wi-Fi já estava inicializado.");
  } else {
    ESP_ERROR_CHECK(err);
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, NULL);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  char target_ssid[32] = "Darth Maul";
  char target_password[64] = "MyPassword123";
  uint8_t target_max_conn = 4;  
  char target_ip[16] = "192.168.4.1";

  wifi_load_ap_config(target_ssid, target_password, &target_max_conn, target_ip);

  if (netif_ap) {
    esp_netif_dhcps_stop(netif_ap);
    esp_netif_ip_info_t ip_info;
    esp_netif_str_to_ip4(target_ip, &ip_info.ip);
    esp_netif_str_to_ip4(target_ip, &ip_info.gw);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(netif_ap, &ip_info);
    esp_netif_dhcps_start(netif_ap);
  }

  wifi_config_t ap_config = {
    .ap = {
      .channel = 1,
      .max_connection = target_max_conn,
      .beacon_interval = 100,
    },
  };

  strncpy((char *)ap_config.ap.ssid, target_ssid, sizeof(ap_config.ap.ssid));
  ap_config.ap.ssid_len = strlen(target_ssid);
  strncpy((char *)ap_config.ap.password, target_password, sizeof(ap_config.ap.password));

  if (strlen(target_password) == 0) {
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
  } else {
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  }

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

  ESP_ERROR_CHECK(esp_wifi_start());

  if (wifi_mutex == NULL) {
    wifi_mutex = xSemaphoreCreateMutex();
  }

  ESP_LOGI(TAG, "Wi-Fi AP iniciado com SSID: %s", target_ssid);
}

void wifi_service_scan(void) {
  if (wifi_mutex == NULL) {
    wifi_mutex = xSemaphoreCreateMutex();
  }

  if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Falha ao obter mutex Wi-Fi para scan");
    return;
  }

  // Configuração do scan
  wifi_scan_config_t scan_config = {
    .ssid = NULL, 
    .bssid = NULL, 
    .channel = 0, 
    .show_hidden = true,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .scan_time.active.min = 100,
    .scan_time.active.max = 300
  };

  ESP_LOGI(TAG, "Iniciando scan de redes (Service)...");

  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao iniciar scan: %s", esp_err_to_name(ret));
    led_blink_red();
    xSemaphoreGive(wifi_mutex);
    return;
  }

  uint16_t ap_count = WIFI_SCAN_LIST_SIZE;
  ret = esp_wifi_scan_get_ap_records(&ap_count, stored_aps);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao obter resultados do scan: %s", esp_err_to_name(ret));
    led_blink_red();
  } else {
    stored_ap_count = ap_count;
    ESP_LOGI(TAG, "Encontrados %d pontos de acesso.", stored_ap_count);
    led_blink_blue();
  }

  xSemaphoreGive(wifi_mutex);
}

uint16_t wifi_service_get_ap_count(void) {
  return stored_ap_count;
}

wifi_ap_record_t* wifi_service_get_ap_record(uint16_t index) {
  if (index < stored_ap_count) {
    return &stored_aps[index];
  }
  return NULL;
}

esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password) {
  if (ssid == NULL) {
    ESP_LOGE(TAG, "SSID can't be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Configurating Wi-Fi connection to:: %s", ssid);

  wifi_config_t wifi_config = {0};

  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));

  if (password) {
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  } else {
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  esp_wifi_disconnect(); 

  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to config STA: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init connection: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Connection request send with success.");
  return ESP_OK;
}

void wifi_deinit(void) {
  ESP_LOGI(TAG, "Iniciando desativação do Wi-Fi...");
  esp_err_t err;

  err = esp_wifi_stop();
  if (err == ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGW(TAG, "Wi-Fi já estava desativado ou não inicializado. Abortando deinit.");
    return; // Se não tá init, não tem nada pra limpar
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Erro ao parar Wi-Fi: %s", esp_err_to_name(err));
  }

  err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Aviso: Falha ao desregistrar handler WIFI_EVENT (pode já ter sido removido): %s", esp_err_to_name(err));
  }

  err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Aviso: Falha ao desregistrar handler IP_EVENT (pode já ter sido removido): %s", esp_err_to_name(err));
  }

  err = esp_wifi_deinit();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Erro ao desinicializar driver Wi-Fi: %s", esp_err_to_name(err));
  }

  if (wifi_mutex != NULL) {
    vSemaphoreDelete(wifi_mutex);
    wifi_mutex = NULL;
  }

  stored_ap_count = 0;
  memset(stored_aps, 0, sizeof(stored_aps));

  ESP_LOGI(TAG, "Wi-Fi desativado e recursos liberados.");
}

void wifi_start(void){
  esp_err_t err;
  err = esp_wifi_start();
  if(err != ESP_OK){
    ESP_LOGE(TAG, "Error to start wifi: %s", esp_err_to_name(err));
  }
}

void wifi_stop(void){
  esp_err_t err;
  err = esp_wifi_stop();
  if(err != ESP_OK){
    ESP_LOGE(TAG, "Error to stop wifi: %s", esp_err_to_name(err));
  }

  stored_ap_count = 0;
  memset(stored_aps, 0, sizeof(stored_aps));
  ESP_LOGI(TAG, "WiFi stopped and cleaned static data");

}

static void wifi_load_ap_config(char* ssid, char* passwd, uint8_t* max_conn, char* ip_addr){
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char*)storage_assets_load_file(WIFI_AP_CONFIG_FILE, &size);

  if (buffer != NULL) {
    cJSON *root = cJSON_Parse(buffer);
    if (root) {
      cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
      cJSON *j_pass = cJSON_GetObjectItem(root, "password");
      cJSON *j_conn = cJSON_GetObjectItem(root, "max_conn");
      cJSON *j_ip = cJSON_GetObjectItem(root, "ip_addr");

      if (cJSON_IsString(j_ssid) && (strlen(j_ssid->valuestring) > 0)) {
        strncpy(ssid, j_ssid->valuestring, 31);
        ssid[31] = '\0';
      }
      if (cJSON_IsString(j_pass)) {
        strncpy(passwd, j_pass->valuestring, 63);
        passwd[63] = '\0';
      }
      if (cJSON_IsNumber(j_conn)){
        *max_conn = (uint8_t)j_conn->valueint;
      }
      if (cJSON_IsString(j_ip) && (strlen(j_ip->valuestring) > 0)) {
        strncpy(ip_addr, j_ip->valuestring, 15);
        ip_addr[15] = '\0';
      }

      cJSON_Delete(root);
      ESP_LOGI(TAG, "Configurações carregadas do storage assets com sucesso.");
    }
    free(buffer);
  } else {
    ESP_LOGW(TAG, "Arquivo de config não encontrado no assets. Usando padrões.");
  }
}

esp_err_t wifi_save_ap_config(const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr) {
  if (ssid == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    ESP_LOGE(TAG, "Falha ao alocar memória para o JSON");
    return ESP_ERR_NO_MEM;
  }

  cJSON_AddStringToObject(root, "ssid", ssid);
  cJSON_AddStringToObject(root, "password", (password != NULL) ? password : "");
  cJSON_AddNumberToObject(root, "max_conn", max_conn);
  if (ip_addr != NULL && strlen(ip_addr) > 0) {
    cJSON_AddStringToObject(root, "ip_addr", ip_addr);
  }

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = storage_write_string(WIFI_AP_CONFIG_PATH, json_string);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Configuração salva com sucesso em: %s", WIFI_AP_CONFIG_PATH);
  } else {
    ESP_LOGE(TAG, "Erro ao gravar arquivo de configuração: %s", esp_err_to_name(err));
  }

  free(json_string);
  cJSON_Delete(root);

  return err;
}
