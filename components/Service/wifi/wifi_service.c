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
#include <stdbool.h>
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
#define WIFI_KNOWN_NETWORKS_FILE "storage/wifi/know_networks.json"
#define WIFI_KNOWN_NETWORKS_PATH "/assets/" WIFI_KNOWN_NETWORKS_FILE

static wifi_ap_record_t stored_aps[WIFI_SCAN_LIST_SIZE];
static uint16_t stored_ap_count = 0;
static SemaphoreHandle_t wifi_mutex = NULL;
static bool wifi_active = false;
static bool wifi_connected = false;
static void wifi_load_ap_config(char* ssid, char* passwd, uint8_t* max_conn, char* ip_addr, bool* enabled);

static void wifi_save_known_network(const char *ssid, const char *password) {
  if (ssid == NULL) return;

  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char*)storage_assets_load_file(WIFI_KNOWN_NETWORKS_FILE, &size);
  cJSON *root = NULL;

  if (buffer != NULL) {
    root = cJSON_Parse(buffer);
    free(buffer);
  }

  if (root == NULL) {
    root = cJSON_CreateArray();
  } else if (!cJSON_IsArray(root)) {
    cJSON_Delete(root);
    root = cJSON_CreateArray();
  }

  bool found = false;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, root) {
    cJSON *j_ssid = cJSON_GetObjectItem(item, "ssid");
    if (cJSON_IsString(j_ssid) && strcmp(j_ssid->valuestring, ssid) == 0) {
      // Update existing
      cJSON_ReplaceItemInObject(item, "password", cJSON_CreateString((password) ? password : ""));
      found = true;
      break;
    }
  }

  if (!found) {
    // Add new
    cJSON *new_item = cJSON_CreateObject();
    cJSON_AddStringToObject(new_item, "ssid", ssid);
    cJSON_AddStringToObject(new_item, "password", (password) ? password : "");
    cJSON_AddItemToArray(root, new_item);
  }

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string != NULL) {
    esp_err_t err = storage_write_string(WIFI_KNOWN_NETWORKS_PATH, json_string);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Known network saved: %s", ssid);
    } else {
      ESP_LOGE(TAG, "Failed to save known network: %s", esp_err_to_name(err));
    }
    free(json_string);
  }

  cJSON_Delete(root);
}

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
    hopper_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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
    ESP_LOGI(TAG, "Station connected to AP, MAC: " MACSTR, MAC2STR(event->mac));
    led_blink_green();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    led_blink_red();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected from AP");
    wifi_connected = false;
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
    ESP_LOGI(TAG, "IP assigned to station connected to AP");
    led_blink_green();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Got IP address, Wi-Fi connected");
    wifi_connected = true;
  }
}

void wifi_change_to_hotspot(const char *new_ssid) {
  ESP_LOGI(TAG, "Attempting to change AP SSID to: %s (open)", new_ssid);

  esp_err_t err = esp_wifi_stop();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to stop Wi-Fi: %s", esp_err_to_name(err));
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
    ESP_LOGE(TAG, "Failed to set new Wi-Fi AP configuration: %s", esp_err_to_name(err));
    led_blink_red(); 
    return;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Wi-Fi with new configuration: %s", esp_err_to_name(err));
    led_blink_red(); 
    return;
  }

  ESP_LOGI(TAG, "Wi-Fi AP SSID successfully changed to: %s (open)", new_ssid);
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
    ESP_LOGW(TAG, "Wi-Fi driver already initialized.");
  } else {
    ESP_ERROR_CHECK(err);
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  char target_ssid[32] = "Darth Maul";
  char target_password[64] = "MyPassword123";
  uint8_t target_max_conn = 4;  
  char target_ip[16] = "192.168.4.1";
  bool target_enabled = true;

  wifi_load_ap_config(target_ssid, target_password, &target_max_conn, target_ip, &target_enabled);

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

  if (target_enabled) {
      ESP_ERROR_CHECK(esp_wifi_start());
      wifi_active = true;
      ESP_LOGI(TAG, "Wi-Fi AP started with SSID: %s", target_ssid);
  } else {
      ESP_LOGI(TAG, "Wi-Fi AP initialized but disabled by config.");
  }

  if (wifi_mutex == NULL) {
    wifi_mutex = xSemaphoreCreateMutex();
  }
}

void wifi_service_scan(void) {
  if (wifi_mutex == NULL) {
    wifi_mutex = xSemaphoreCreateMutex();
  }

  if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to obtain Wi-Fi mutex for scan");
    return;
  }

  wifi_service_promiscuous_stop();
  wifi_service_stop_channel_hopping();

  wifi_scan_config_t scan_config = {
    .ssid = NULL, 
    .bssid = NULL, 
    .channel = 0, 
    .show_hidden = true,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .scan_time.active.min = 100,
    .scan_time.active.max = 300
  };

  ESP_LOGI(TAG, "Starting network scan (Service)...");

  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
    led_blink_red();
    xSemaphoreGive(wifi_mutex);
    return;
  }

  uint16_t ap_count = WIFI_SCAN_LIST_SIZE;
  ret = esp_wifi_scan_get_ap_records(&ap_count, stored_aps);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
    led_blink_red();
  } else {
    stored_ap_count = ap_count;
    ESP_LOGI(TAG, "Found %d access points.", stored_ap_count);
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

  // Save the network to known networks
  wifi_save_known_network(ssid, password);

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

bool wifi_service_is_connected(void) {
    return wifi_connected;
}

bool wifi_service_is_active(void) {
    return wifi_active;
}

void wifi_deinit(void) {
  ESP_LOGI(TAG, "Starting Wi-Fi deactivation...");
  esp_err_t err;

  err = esp_wifi_stop();
  if (err == ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGW(TAG, "Wi-Fi was already deactivated or not initialized. Aborting deinit.");
    return; 
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error stopping Wi-Fi: %s", esp_err_to_name(err));
  }

  wifi_active = false;
  wifi_connected = false;

  err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Warning: Failed to unregister handler WIFI_EVENT (may have already been removed): %s", esp_err_to_name(err));
  }

  err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Warning: Failed to unregister handler IP_EVENT (may have already been removed): %s", esp_err_to_name(err));
  }

  err = esp_wifi_deinit();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error deinitializing Wi-Fi driver: %s", esp_err_to_name(err));
  }

  if (wifi_mutex != NULL) {
    vSemaphoreDelete(wifi_mutex);
    wifi_mutex = NULL;
  }

  stored_ap_count = 0;
  memset(stored_aps, 0, sizeof(stored_aps));

  ESP_LOGI(TAG, "Wi-Fi deactivated and resources released.");
}

void wifi_start(void){
  esp_err_t err;
  err = esp_wifi_start();
  if(err == ESP_OK){
    wifi_active = true;
  } else {
    ESP_LOGE(TAG, "Error starting Wi-Fi: %s", esp_err_to_name(err));
  }
}

void wifi_stop(void){
  esp_err_t err;
  err = esp_wifi_stop();
  if(err == ESP_OK){
    wifi_active = false;
    wifi_connected = false;
  } else {
    ESP_LOGE(TAG, "Error stopping Wi-Fi: %s", esp_err_to_name(err));
  }

  stored_ap_count = 0;
  memset(stored_aps, 0, sizeof(stored_aps));
  ESP_LOGI(TAG, "Wi-Fi stopped and cleaned static data");

}

static void wifi_load_ap_config(char* ssid, char* passwd, uint8_t* max_conn, char* ip_addr, bool* enabled){
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
      cJSON *j_enabled = cJSON_GetObjectItem(root, "enabled");

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
      if (cJSON_IsBool(j_enabled)) {
          *enabled = cJSON_IsTrue(j_enabled);
      }

      cJSON_Delete(root);
      ESP_LOGI(TAG, "Configurations loaded from asset storage successfully.");
    }
    free(buffer);
  } else {
    ESP_LOGW(TAG, "Config file not found in assets. Using defaults.");
  }
}

esp_err_t wifi_save_ap_config(const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled) {
  if (ssid == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for JSON");
    return ESP_ERR_NO_MEM;
  }

  cJSON_AddStringToObject(root, "ssid", ssid);
  cJSON_AddStringToObject(root, "password", (password != NULL) ? password : "");
  cJSON_AddNumberToObject(root, "max_conn", max_conn);
  if (ip_addr != NULL && strlen(ip_addr) > 0) {
    cJSON_AddStringToObject(root, "ip_addr", ip_addr);
  }
  cJSON_AddBoolToObject(root, "enabled", enabled);

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = storage_write_string(WIFI_AP_CONFIG_PATH, json_string);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Configuration saved successfully to: %s", WIFI_AP_CONFIG_PATH);
    
    // Apply state change based on enabled flag
    if (enabled && !wifi_active) {
        wifi_start();
    } else if (!enabled && wifi_active) {
        wifi_stop();
    }
  } else {
    ESP_LOGE(TAG, "Error writing configuration file: %s", esp_err_to_name(err));
  }

  free(json_string);
  cJSON_Delete(root);

  return err;
}

static void wifi_get_config_defaults(char *ssid, char *password, uint8_t *max_conn, char *ip_addr, bool *enabled) {
    strncpy(ssid, "Darth Maul", 32);
    strncpy(password, "MyPassword123", 64);
    *max_conn = 4;
    strncpy(ip_addr, "192.168.4.1", 16);
    *enabled = true;
    
    // Override with file values if available
    wifi_load_ap_config(ssid, password, max_conn, ip_addr, enabled);
}

esp_err_t wifi_set_wifi_enabled(bool enabled) {
    char ssid[32]; char password[64]; uint8_t max_conn; char ip_addr[16]; bool curr_enabled;
    wifi_get_config_defaults(ssid, password, &max_conn, ip_addr, &curr_enabled);
    return wifi_save_ap_config(ssid, password, max_conn, ip_addr, enabled);
}

esp_err_t wifi_set_ap_ssid(const char *ssid) {
    char curr_ssid[32]; char password[64]; uint8_t max_conn; char ip_addr[16]; bool enabled;
    wifi_get_config_defaults(curr_ssid, password, &max_conn, ip_addr, &enabled);
    return wifi_save_ap_config(ssid, password, max_conn, ip_addr, enabled);
}

esp_err_t wifi_set_ap_password(const char *password) {
    char ssid[32]; char curr_password[64]; uint8_t max_conn; char ip_addr[16]; bool enabled;
    wifi_get_config_defaults(ssid, curr_password, &max_conn, ip_addr, &enabled);
    return wifi_save_ap_config(ssid, password, max_conn, ip_addr, enabled);
}

esp_err_t wifi_set_ap_max_conn(uint8_t max_conn) {
    char ssid[32]; char password[64]; uint8_t curr_max_conn; char ip_addr[16]; bool enabled;
    wifi_get_config_defaults(ssid, password, &curr_max_conn, ip_addr, &enabled);
    return wifi_save_ap_config(ssid, password, max_conn, ip_addr, enabled);
}

esp_err_t wifi_set_ap_ip(const char *ip_addr) {
    char ssid[32]; char password[64]; uint8_t max_conn; char curr_ip_addr[16]; bool enabled;
    wifi_get_config_defaults(ssid, password, &max_conn, curr_ip_addr, &enabled);
    return wifi_save_ap_config(ssid, password, max_conn, ip_addr, enabled);
}
