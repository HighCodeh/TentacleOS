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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "nvs_flash.h"

#include "cJSON.h"
#include "led_control.h"
#include "storage_assets.h"

static const char *TAG = "WIFI_SERVICE";

#define HOPPER_STACK_SIZE       4096
#define HOPPER_TASK_PRIORITY    5
#define HOPPER_DELAY_MS         250
#define MAX_WIFI_CHANNEL        13
#define SCAN_MUTEX_TIMEOUT_MS   1000
#define SCAN_ACTIVE_MIN_MS      100
#define SCAN_ACTIVE_MAX_MS      300
#define SSID_MAX_LEN            32
#define PASSWORD_MAX_LEN        64
#define IP_ADDR_MAX_LEN         16
#define DEFAULT_MAX_CONN        4
#define DEFAULT_AP_CHANNEL      1
#define DEFAULT_BEACON_INTERVAL 100
#define WIFI_CMD_TIMEOUT_MS     100

static wifi_ap_record_t s_stored_aps[WIFI_SCAN_LIST_SIZE];
static uint16_t s_stored_ap_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_is_active = false;
static bool s_is_connected = false;
static TaskHandle_t s_hopper_task_handle = NULL;
static StackType_t *s_hopper_task_stack = NULL;
static StaticTask_t *s_hopper_task_tcb = NULL;

static void
event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void channel_hopper_task(void *pvParameters);
static void save_known_network(const char *ssid, const char *password);
static bool get_known_network_password(const char *ssid, char *out_password, size_t buffer_size);
static void load_ap_config(
    char *out_ssid, char *out_passwd, uint8_t *out_max_conn, char *out_ip_addr, bool *out_enabled);
static void get_config_defaults(char *out_ssid,
                                char *out_password,
                                uint8_t *out_max_conn,
                                char *out_ip_addr,
                                bool *out_enabled);

// Public function implementations

void wifi_service_init(void) {
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
    ESP_LOGW(TAG, "Wi-Fi driver already initialized");
  } else {
    ESP_ERROR_CHECK(err);
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  char target_ssid[SSID_MAX_LEN] = "Darth Maul";
  char target_password[PASSWORD_MAX_LEN] = "MyPassword123";
  uint8_t target_max_conn = DEFAULT_MAX_CONN;
  char target_ip[IP_ADDR_MAX_LEN] = "192.168.4.1";
  bool is_enabled = true;

  load_ap_config(target_ssid, target_password, &target_max_conn, target_ip, &is_enabled);

  if (netif_ap != NULL) {
    esp_netif_dhcps_stop(netif_ap);
    esp_netif_ip_info_t ip_info;
    esp_netif_str_to_ip4(target_ip, &ip_info.ip);
    esp_netif_str_to_ip4(target_ip, &ip_info.gw);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(netif_ap, &ip_info);
    esp_netif_dhcps_start(netif_ap);
  }

  wifi_config_t ap_config = {
      .ap =
          {
              .channel = DEFAULT_AP_CHANNEL,
              .max_connection = target_max_conn,
              .beacon_interval = DEFAULT_BEACON_INTERVAL,
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

  if (is_enabled) {
    ESP_ERROR_CHECK(esp_wifi_start());
    s_is_active = true;
    ESP_LOGI(TAG, "Wi-Fi AP started with SSID: %s", target_ssid);
  } else {
    ESP_LOGI(TAG, "Wi-Fi AP initialized but disabled by config");
  }

  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void wifi_service_deinit(void) {
  ESP_LOGI(TAG, "Starting Wi-Fi deactivation");
  esp_err_t err;

  err = esp_wifi_stop();
  if (err == ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGW(TAG, "Wi-Fi was already deactivated or not initialized");
    return;
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error stopping Wi-Fi: %s", esp_err_to_name(err));
  }

  s_is_active = false;
  s_is_connected = false;

  err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to unregister WIFI_EVENT handler: %s", esp_err_to_name(err));
  }

  err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &event_handler);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to unregister IP_EVENT handler: %s", esp_err_to_name(err));
  }

  err = esp_wifi_deinit();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error deinitializing Wi-Fi driver: %s", esp_err_to_name(err));
  }

  if (s_mutex != NULL) {
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
  }

  s_stored_ap_count = 0;
  memset(s_stored_aps, 0, sizeof(s_stored_aps));

  ESP_LOGI(TAG, "Wi-Fi deactivated and resources released");
}

void wifi_service_stop(void) {
  esp_err_t err = esp_wifi_stop();
  if (err == ESP_OK) {
    s_is_active = false;
    s_is_connected = false;
  } else {
    ESP_LOGE(TAG, "Error stopping Wi-Fi: %s", esp_err_to_name(err));
  }

  s_stored_ap_count = 0;
  memset(s_stored_aps, 0, sizeof(s_stored_aps));
  ESP_LOGI(TAG, "Wi-Fi stopped");
}

void wifi_service_start(void) {
  esp_err_t err = esp_wifi_start();
  if (err == ESP_OK) {
    s_is_active = true;
  } else {
    ESP_LOGE(TAG, "Error starting Wi-Fi: %s", esp_err_to_name(err));
  }
}

void wifi_service_change_to_hotspot(const char *new_ssid) {
  ESP_LOGI(TAG, "Changing AP SSID to: %s (open)", new_ssid);

  esp_err_t err = esp_wifi_stop();
  vTaskDelay(pdMS_TO_TICKS(WIFI_CMD_TIMEOUT_MS));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to stop Wi-Fi: %s", esp_err_to_name(err));
    led_blink_red();
    return;
  }

  wifi_config_t new_ap_config = {
      .ap =
          {
              .ssid_len = strlen(new_ssid),
              .channel = DEFAULT_AP_CHANNEL,
              .authmode = WIFI_AUTH_OPEN,
              .max_connection = DEFAULT_MAX_CONN,
          },
  };

  esp_wifi_set_mode(WIFI_MODE_AP);

  strncpy((char *)new_ap_config.ap.ssid, new_ssid, sizeof(new_ap_config.ap.ssid) - 1);
  new_ap_config.ap.ssid[sizeof(new_ap_config.ap.ssid) - 1] = '\0';
  new_ap_config.ap.password[0] = '\0';

  err = esp_wifi_set_config(WIFI_IF_AP, &new_ap_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AP configuration: %s", esp_err_to_name(err));
    led_blink_red();
    return;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
    led_blink_red();
    return;
  }

  ESP_LOGI(TAG, "Wi-Fi AP SSID changed to: %s (open)", new_ssid);
  led_blink_green();
}

esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password) {
  if (ssid == NULL) {
    ESP_LOGE(TAG, "SSID must not be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Configuring Wi-Fi connection to: %s", ssid);

  char effective_password[PASSWORD_MAX_LEN] = {0};
  bool has_password = false;

  if (password != NULL) {
    strncpy(effective_password, password, sizeof(effective_password) - 1);
    save_known_network(ssid, password);
    has_password = true;
  } else {
    if (get_known_network_password(ssid, effective_password, sizeof(effective_password))) {
      ESP_LOGI(TAG, "Found known password for %s", ssid);
      has_password = true;
    } else {
      ESP_LOGI(TAG, "No known password for %s, assuming open network", ssid);
      has_password = false;
    }
  }

  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));

  if (has_password && strlen(effective_password) > 0) {
    strncpy((char *)wifi_config.sta.password, effective_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  } else {
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  esp_wifi_disconnect();

  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure STA: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Connection request sent");
  return ESP_OK;
}

bool wifi_service_is_connected(void) {
  return s_is_connected;
}

bool wifi_service_is_active(void) {
  return s_is_active;
}

const char *wifi_service_get_connected_ssid(void) {
  static char s_connected_ssid[SSID_MAX_LEN + 1];

  if (!s_is_connected) {
    return NULL;
  }

  wifi_config_t config;
  if (esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK) {
    strncpy(s_connected_ssid, (char *)config.sta.ssid, SSID_MAX_LEN);
    s_connected_ssid[SSID_MAX_LEN] = '\0';
    return s_connected_ssid;
  }
  return NULL;
}

void wifi_service_scan(void) {
  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(SCAN_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to obtain mutex for scan");
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
      .scan_time.active.min = SCAN_ACTIVE_MIN_MS,
      .scan_time.active.max = SCAN_ACTIVE_MAX_MS,
  };

  ESP_LOGI(TAG, "Starting network scan");

  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
    led_blink_red();
    xSemaphoreGive(s_mutex);
    return;
  }

  uint16_t ap_count = WIFI_SCAN_LIST_SIZE;
  ret = esp_wifi_scan_get_ap_records(&ap_count, s_stored_aps);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
    led_blink_red();
  } else {
    s_stored_ap_count = ap_count;
    ESP_LOGI(TAG, "Found %d access points", s_stored_ap_count);
    led_blink_blue();
  }

  xSemaphoreGive(s_mutex);
}

uint16_t wifi_service_get_ap_count(void) {
  return s_stored_ap_count;
}

wifi_ap_record_t *wifi_service_get_ap_record(uint16_t index) {
  if (index < s_stored_ap_count) {
    return &s_stored_aps[index];
  }
  return NULL;
}

esp_err_t wifi_service_save_ap_config(
    const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled) {
  if (ssid == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to allocate JSON object");
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

  esp_err_t err = storage_assets_write_file(WIFI_AP_CONFIG_FILE, json_string);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Configuration saved to: %s", WIFI_AP_CONFIG_FILE);

    if (enabled && !s_is_active) {
      wifi_service_start();
    } else if (!enabled && s_is_active) {
      wifi_service_stop();
    }
  } else {
    ESP_LOGE(TAG, "Error writing configuration: %s", esp_err_to_name(err));
  }

  free(json_string);
  cJSON_Delete(root);

  return err;
}

esp_err_t wifi_service_set_enabled(bool enabled) {
  char ssid[SSID_MAX_LEN];
  char password[PASSWORD_MAX_LEN];
  uint8_t max_conn;
  char ip_addr[IP_ADDR_MAX_LEN];
  bool curr_enabled;

  get_config_defaults(ssid, password, &max_conn, ip_addr, &curr_enabled);
  return wifi_service_save_ap_config(ssid, password, max_conn, ip_addr, enabled);
}

esp_err_t wifi_service_set_ap_ssid(const char *ssid) {
  char curr_ssid[SSID_MAX_LEN];
  char password[PASSWORD_MAX_LEN];
  uint8_t max_conn;
  char ip_addr[IP_ADDR_MAX_LEN];
  bool is_enabled;

  get_config_defaults(curr_ssid, password, &max_conn, ip_addr, &is_enabled);
  return wifi_service_save_ap_config(ssid, password, max_conn, ip_addr, is_enabled);
}

esp_err_t wifi_service_set_ap_password(const char *password) {
  char ssid[SSID_MAX_LEN];
  char curr_password[PASSWORD_MAX_LEN];
  uint8_t max_conn;
  char ip_addr[IP_ADDR_MAX_LEN];
  bool is_enabled;

  get_config_defaults(ssid, curr_password, &max_conn, ip_addr, &is_enabled);
  return wifi_service_save_ap_config(ssid, password, max_conn, ip_addr, is_enabled);
}

esp_err_t wifi_service_set_ap_max_conn(uint8_t max_conn) {
  char ssid[SSID_MAX_LEN];
  char password[PASSWORD_MAX_LEN];
  uint8_t curr_max_conn;
  char ip_addr[IP_ADDR_MAX_LEN];
  bool is_enabled;

  get_config_defaults(ssid, password, &curr_max_conn, ip_addr, &is_enabled);
  return wifi_service_save_ap_config(ssid, password, max_conn, ip_addr, is_enabled);
}

esp_err_t wifi_service_set_ap_ip(const char *ip_addr) {
  char ssid[SSID_MAX_LEN];
  char password[PASSWORD_MAX_LEN];
  uint8_t max_conn;
  char curr_ip_addr[IP_ADDR_MAX_LEN];
  bool is_enabled;

  get_config_defaults(ssid, password, &max_conn, curr_ip_addr, &is_enabled);
  return wifi_service_save_ap_config(ssid, password, max_conn, ip_addr, is_enabled);
}

void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter) {
  esp_err_t err;

  if (filter != NULL) {
    err = esp_wifi_set_promiscuous_filter(filter);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set promiscuous filter: %s", esp_err_to_name(err));
    }
  }

  if (cb != NULL) {
    err = esp_wifi_set_promiscuous_rx_cb(cb);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set promiscuous callback: %s", esp_err_to_name(err));
    }
  }

  err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable promiscuous mode: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Promiscuous mode enabled");
  }
}

void wifi_service_promiscuous_stop(void) {
  esp_err_t err = esp_wifi_set_promiscuous(false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to disable promiscuous mode: %s", esp_err_to_name(err));
  }

  esp_wifi_set_promiscuous_rx_cb(NULL);
  ESP_LOGI(TAG, "Promiscuous mode disabled");
}

void wifi_service_start_channel_hopping(void) {
  if (s_hopper_task_handle != NULL) {
    ESP_LOGW(TAG, "Channel hopping already running");
    return;
  }

  if (s_hopper_task_stack == NULL) {
    s_hopper_task_stack =
        (StackType_t *)heap_caps_malloc(HOPPER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  }
  if (s_hopper_task_tcb == NULL) {
    s_hopper_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t),
                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (s_hopper_task_stack == NULL || s_hopper_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate channel hopper in PSRAM, using internal");

    free(s_hopper_task_stack);
    s_hopper_task_stack = NULL;
    free(s_hopper_task_tcb);
    s_hopper_task_tcb = NULL;

    xTaskCreate(channel_hopper_task,
                "chan_hopper_srv",
                HOPPER_STACK_SIZE,
                NULL,
                HOPPER_TASK_PRIORITY,
                &s_hopper_task_handle);
  } else {
    s_hopper_task_handle = xTaskCreateStatic(channel_hopper_task,
                                             "chan_hopper_srv",
                                             HOPPER_STACK_SIZE,
                                             NULL,
                                             HOPPER_TASK_PRIORITY,
                                             s_hopper_task_stack,
                                             s_hopper_task_tcb);
  }

  if (s_hopper_task_handle != NULL) {
    ESP_LOGI(TAG, "Channel hopping started");
  } else {
    ESP_LOGE(TAG, "Failed to start channel hopping task");
  }
}

void wifi_service_stop_channel_hopping(void) {
  if (s_hopper_task_handle != NULL) {
    vTaskDelete(s_hopper_task_handle);
    s_hopper_task_handle = NULL;
  }

  free(s_hopper_task_stack);
  s_hopper_task_stack = NULL;

  free(s_hopper_task_tcb);
  s_hopper_task_tcb = NULL;

  ESP_LOGI(TAG, "Channel hopping stopped");
}

// Static function implementations

static void
event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "Station connected to AP, MAC: " MACSTR, MAC2STR(event->mac));
    led_blink_green();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    led_blink_red();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected from AP");
    s_is_connected = false;
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
    ESP_LOGI(TAG, "IP assigned to station connected to AP");
    led_blink_green();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Got IP address, Wi-Fi connected");
    s_is_connected = true;
  }
}

static void channel_hopper_task(void *pvParameters) {
  uint8_t channel = 1;
  while (1) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    channel++;
    if (channel > MAX_WIFI_CHANNEL) {
      channel = 1;
    }
    vTaskDelay(pdMS_TO_TICKS(HOPPER_DELAY_MS));
  }
}

static void save_known_network(const char *ssid, const char *password) {
  if (ssid == NULL) {
    return;
  }

  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char *)storage_assets_load_file(WIFI_KNOWN_NETWORKS_FILE, &size);
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

  bool is_found = false;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, root) {
    cJSON *j_ssid = cJSON_GetObjectItem(item, "ssid");
    if (cJSON_IsString(j_ssid) && strcmp(j_ssid->valuestring, ssid) == 0) {
      cJSON_ReplaceItemInObject(
          item, "password", cJSON_CreateString((password != NULL) ? password : ""));
      is_found = true;
      break;
    }
  }

  if (!is_found) {
    cJSON *new_item = cJSON_CreateObject();
    cJSON_AddStringToObject(new_item, "ssid", ssid);
    cJSON_AddStringToObject(new_item, "password", (password != NULL) ? password : "");
    cJSON_AddItemToArray(root, new_item);
  }

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string != NULL) {
    esp_err_t err = storage_assets_write_file(WIFI_KNOWN_NETWORKS_FILE, json_string);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Known network saved: %s", ssid);
    } else {
      ESP_LOGE(TAG, "Failed to save known network: %s", esp_err_to_name(err));
    }
    free(json_string);
  }

  cJSON_Delete(root);
}

static bool get_known_network_password(const char *ssid, char *out_password, size_t buffer_size) {
  if (ssid == NULL || out_password == NULL) {
    return false;
  }

  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char *)storage_assets_load_file(WIFI_KNOWN_NETWORKS_FILE, &size);
  if (buffer == NULL) {
    return false;
  }

  cJSON *root = cJSON_Parse(buffer);
  free(buffer);

  if (root == NULL || !cJSON_IsArray(root)) {
    if (root != NULL) {
      cJSON_Delete(root);
    }
    return false;
  }

  bool is_found = false;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, root) {
    cJSON *j_ssid = cJSON_GetObjectItem(item, "ssid");
    if (cJSON_IsString(j_ssid) && strcmp(j_ssid->valuestring, ssid) == 0) {
      cJSON *j_pass = cJSON_GetObjectItem(item, "password");
      if (cJSON_IsString(j_pass)) {
        strncpy(out_password, j_pass->valuestring, buffer_size - 1);
        out_password[buffer_size - 1] = '\0';
        is_found = true;
      }
      break;
    }
  }

  cJSON_Delete(root);
  return is_found;
}

static void load_ap_config(
    char *out_ssid, char *out_passwd, uint8_t *out_max_conn, char *out_ip_addr, bool *out_enabled) {
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char *)storage_assets_load_file(WIFI_AP_CONFIG_FILE, &size);

  if (buffer != NULL) {
    cJSON *root = cJSON_Parse(buffer);
    if (root != NULL) {
      cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
      cJSON *j_pass = cJSON_GetObjectItem(root, "password");
      cJSON *j_conn = cJSON_GetObjectItem(root, "max_conn");
      cJSON *j_ip = cJSON_GetObjectItem(root, "ip_addr");
      cJSON *j_enabled = cJSON_GetObjectItem(root, "enabled");

      if (cJSON_IsString(j_ssid) && strlen(j_ssid->valuestring) > 0) {
        strncpy(out_ssid, j_ssid->valuestring, SSID_MAX_LEN - 1);
        out_ssid[SSID_MAX_LEN - 1] = '\0';
      }
      if (cJSON_IsString(j_pass)) {
        strncpy(out_passwd, j_pass->valuestring, PASSWORD_MAX_LEN - 1);
        out_passwd[PASSWORD_MAX_LEN - 1] = '\0';
      }
      if (cJSON_IsNumber(j_conn)) {
        *out_max_conn = (uint8_t)j_conn->valueint;
      }
      if (cJSON_IsString(j_ip) && strlen(j_ip->valuestring) > 0) {
        strncpy(out_ip_addr, j_ip->valuestring, IP_ADDR_MAX_LEN - 1);
        out_ip_addr[IP_ADDR_MAX_LEN - 1] = '\0';
      }
      if (cJSON_IsBool(j_enabled)) {
        *out_enabled = cJSON_IsTrue(j_enabled);
      }

      cJSON_Delete(root);
      ESP_LOGI(TAG, "Configuration loaded from asset storage");
    }
    free(buffer);
  } else {
    ESP_LOGW(TAG, "Config file not found, using defaults");
  }
}

static void get_config_defaults(char *out_ssid,
                                char *out_password,
                                uint8_t *out_max_conn,
                                char *out_ip_addr,
                                bool *out_enabled) {
  strncpy(out_ssid, "Darth Maul", SSID_MAX_LEN);
  strncpy(out_password, "MyPassword123", PASSWORD_MAX_LEN);
  *out_max_conn = DEFAULT_MAX_CONN;
  strncpy(out_ip_addr, "192.168.4.1", IP_ADDR_MAX_LEN);
  *out_enabled = true;

  load_ap_config(out_ssid, out_password, out_max_conn, out_ip_addr, out_enabled);
}
