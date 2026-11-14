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

#include "wifi_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lwip/inet.h"
#include "nvs_flash.h"

#include "cJSON.h"
#include "led_control.h"
#include "storage_assets.h"
#include "spi_protocol.h"

static const char *TAG = "WIFI_SERVICE";

#define HOPPER_STACK_SIZE        4096
#define HOPPER_DELAY_MS          250
#define CAPTURE_HOP_LAB_CAPACITY 64
#define CAPTURE_HOP_LAB_ATTEMPTS 3
#define CAPTURE_HOP_LAB_HOLD_US  15000000LL

#define CAPTURE_HOP_IE_HEADER_LEN         2
#define CAPTURE_HOP_SSID_TAG              0
#define CAPTURE_HOP_RSN_TAG               0x30
#define CAPTURE_HOP_VENDOR_TAG            0xDD
#define CAPTURE_HOP_WPA_OUI_0             0x00
#define CAPTURE_HOP_WPA_OUI_1             0x50
#define CAPTURE_HOP_WPA_OUI_2             0xF2
#define CAPTURE_HOP_WPA_OUI_TYPE          0x01
#define CAPTURE_HOP_WPA_OUI_LEN           4
#define CAPTURE_HOP_SECURITY_WORD_LEN     2
#define CAPTURE_HOP_SECURITY_SELECTOR_LEN 4
#define CAPTURE_HOP_SECURITY_LIST_COUNT   2
#define CAPTURE_HOP_SECURITY_VERSION      1
#define CAPTURE_HOP_BYTE_SHIFT            8
#define CAPTURE_HOP_PMKID_LEN             16
#define CAPTURE_HOP_US_PER_SECOND         1000000LL
#define CAPTURE_HOP_EMPTY_MS              120
#define CAPTURE_HOP_AP_MS                 200
#define CAPTURE_HOP_ASSOC_MS              750
#define CAPTURE_HOP_EAPOL_MS              1000
#define CAPTURE_HOP_MAX_MS                2000
#define CAPTURE_HOP_POLL_MS               20
#define CAPTURE_HOP_CHANNEL_CAPACITY      32
#define CAPTURE_HOP_AP_CAPACITY           64
#define CAPTURE_HOP_BSSID_LEN             6
#define CAPTURE_HOP_DECAY_US              30000000LL
#define CAPTURE_HOP_REVISIT_US            5000000LL
#define CAPTURE_HOP_ACTIVITY_US           1000000LL
#define CAPTURE_HOP_SCORE_MAX             64
#define CAPTURE_HOP_SCORE_BITS            16
#define CAPTURE_HOP_ASSOC_WEIGHT          4
#define CAPTURE_HOP_EAPOL_WEIGHT          8
#define CAPTURE_HOP_AGE_UNIT_US           250000LL
#define CAPTURE_HOP_AGE_MAX               20
#define MAX_WIFI_CHANNEL                  13
#define SCAN_STEP_DWELL_MS                120
#define SCAN_MUTEX_TIMEOUT_MS             1000
#define SCAN_ACTIVE_MIN_MS                100
#define SCAN_ACTIVE_MAX_MS                300
#define SSID_MAX_LEN                      32
#define PASSWORD_MAX_LEN                  64
#define IP_ADDR_MAX_LEN                   16
#define DEFAULT_MAX_CONN                  4
#define DEFAULT_AP_CHANNEL                1
#define DEFAULT_BEACON_INTERVAL           100
#define WIFI_CMD_TIMEOUT_MS               100
#define SCAN_DWELL_MIN_MS                 20
#define SCAN_DWELL_MAX_MS                 1500
#define CAPTURE_BAND_2G                   1
#define CAPTURE_BAND_5G                   2
#define CAPTURE_CHANNEL_2G_MAX            14
#define CAPTURE_US_PER_MS                 1000

static wifi_ap_record_t s_stored_aps[WIFI_SCAN_LIST_SIZE];
static uint16_t s_stored_ap_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_is_active = false;
static bool s_is_connected = false;
static bool s_promiscuous_active = false;
static uint8_t s_analysis_band = 0;
typedef struct {
  bool is_capture;
  bool has_ap_hold;
  bool has_action_pending;
  uint8_t bssid[CAPTURE_HOP_BSSID_LEN];
  uint8_t channel;
  int64_t started_us;
  int64_t deadline_us;
} wifi_service_hop_state_t;

// Scheduling ideas credited to Gutem/pwnagotchi-esp32; see docs/wifi/README.md.
typedef struct {
  uint8_t channel;
  uint16_t score;
  bool is_visited;
  int64_t visited_us;
  int64_t decay_us;
} wifi_service_hop_channel_t;

typedef struct {
  uint8_t bssid[CAPTURE_HOP_BSSID_LEN];
  uint8_t channel;
  uint8_t seen_mask;
  int64_t credited_us[WIFI_SERVICE_CAPTURE_ACTIVITY_COUNT];
  int64_t touched_us;
} wifi_service_hop_ap_t;

typedef struct {
  uint8_t bssid[CAPTURE_HOP_BSSID_LEN];
  uint8_t attempts;
  bool is_captured;
} wifi_service_hop_lab_t;

// Never evict: replacement would reset a BSSID's per-boot budget.
static wifi_service_hop_lab_t s_hop_labs[CAPTURE_HOP_LAB_CAPACITY];
static wifi_service_hop_channel_t *s_hop_channels;
static wifi_service_hop_ap_t *s_hop_aps;
static size_t s_hop_channel_count;
static portMUX_TYPE s_hop_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_service_hop_state_t s_hop_state;
static wifi_service_capture_action_cb_t s_capture_action;
static wifi_service_capture_status_t s_capture_status;
static TaskHandle_t s_hopper_task_handle = NULL;
static StaticSemaphore_t s_hopper_action_storage;
static SemaphoreHandle_t s_hopper_action_mutex;
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

void wifi_service_init(void) {
  esp_err_t err;

  err = nvs_flash_init();
  // Integrated build: never erase existing NVS on an init error.
  // Fail closed below; migration/recovery requires a separate reviewed action.
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

  esp_err_t band_err = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  if (band_err != ESP_OK) {
    ESP_LOGW(TAG, "Could not enable dual-band: %s", esp_err_to_name(band_err));
  }

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

  esp_err_t ap_cfg_err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  if (ap_cfg_err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(ap_cfg_err));
  }

  if (is_enabled) {
    ESP_ERROR_CHECK(esp_wifi_start());
    s_is_active = true;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
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
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error stopping Wi-Fi: %s", esp_err_to_name(err));
  }
  s_is_active = false;
  s_is_connected = false;

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

static wifi_band_mode_t band_mode_from_selector(uint8_t band) {
  if (band == 1)
    return WIFI_BAND_MODE_2G_ONLY;
  if (band == 2)
    return WIFI_BAND_MODE_5G_ONLY;
  return WIFI_BAND_MODE_AUTO;
}

esp_err_t wifi_service_set_analysis_band(uint8_t band) {
  if (band > 2)
    return ESP_ERR_INVALID_ARG;
  esp_err_t err = esp_wifi_set_band_mode(band_mode_from_selector(band));
  if (err == ESP_OK)
    s_analysis_band = band;
  else
    ESP_LOGW(TAG, "Band selection %u failed: %s", band, esp_err_to_name(err));
  return err;
}

void wifi_service_scan(void) {
  wifi_service_scan_ex(0, false, SCAN_ACTIVE_MIN_MS, SCAN_ACTIVE_MAX_MS);
}

void wifi_service_scan_ex(uint8_t band,
                          bool passive,
                          uint16_t dwell_min_ms,
                          uint16_t dwell_max_ms) {
  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(SCAN_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to obtain mutex for scan");
    return;
  }

  wifi_service_promiscuous_stop();
  wifi_service_stop_channel_hopping();

  if (!s_is_active) {
    wifi_service_start();
  }

  if (band > 2)
    band = 0;
  if (dwell_min_ms == 0)
    dwell_min_ms = SCAN_ACTIVE_MIN_MS;
  if (dwell_max_ms == 0)
    dwell_max_ms = SCAN_ACTIVE_MAX_MS;
  if (dwell_min_ms < SCAN_DWELL_MIN_MS)
    dwell_min_ms = SCAN_DWELL_MIN_MS;
  if (dwell_max_ms > SCAN_DWELL_MAX_MS)
    dwell_max_ms = SCAN_DWELL_MAX_MS;
  if (dwell_max_ms < dwell_min_ms)
    dwell_max_ms = dwell_min_ms;

  esp_err_t band_err = esp_wifi_set_band_mode(band_mode_from_selector(band));
  if (band_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to select scan band %u: %s", band, esp_err_to_name(band_err));
    xSemaphoreGive(s_mutex);
    return;
  }

  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = true,
      .scan_type = passive ? WIFI_SCAN_TYPE_PASSIVE : WIFI_SCAN_TYPE_ACTIVE,
  };

  if (passive) {
    scan_config.scan_time.passive = dwell_max_ms;
  } else {
    scan_config.scan_time.active.min = dwell_min_ms;
    scan_config.scan_time.active.max = dwell_max_ms;
  }

  ESP_LOGI(TAG,
           "Starting %s network scan (band=%u dwell=%u-%u ms)",
           passive ? "passive" : "active",
           band,
           dwell_min_ms,
           dwell_max_ms);

  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
    led_blink_red();
    esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    s_analysis_band = 0;
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

  // Survey band selection is scoped to this scan. Restore dual-band so the
  // hotspot/STA service is not accidentally left constrained afterwards.
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  s_analysis_band = 0;

  xSemaphoreGive(s_mutex);
}

esp_err_t wifi_service_scan_step(uint8_t slot) {
  if (slot >= SPI_WIFI_SCAN_SLOT_COUNT)
    return ESP_ERR_INVALID_ARG;
  if (s_mutex == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(SCAN_MUTEX_TIMEOUT_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t err = ESP_OK;
  wifi_band_mode_t previous_band;
  bool has_band = false;
  s_stored_ap_count = 0;
  if (s_promiscuous_active || s_hopper_task_handle != NULL) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  if (!s_is_active)
    wifi_service_start();
  if ((err = esp_wifi_get_band_mode(&previous_band)) != ESP_OK)
    goto cleanup;
  has_band = true;
  if ((err = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO)) != ESP_OK)
    goto cleanup;
  // Bitmap scans intersect with the driver's country policy. 5 GHz is passive,
  // including DFS channels; never expand the regulatory channel mask.
  wifi_scan_config_t config = {
      .show_hidden = true,
      .scan_type = slot < SPI_WIFI_SCAN_2G_SLOTS ? WIFI_SCAN_TYPE_ACTIVE : WIFI_SCAN_TYPE_PASSIVE,
      .scan_time = {.active = {.min = SCAN_STEP_DWELL_MS, .max = SCAN_STEP_DWELL_MS},
                    .passive = SCAN_STEP_DWELL_MS},
      .channel_bitmap = {.ghz_2_channels = 1, .ghz_5_channels = 1},
  };
  if (slot < SPI_WIFI_SCAN_2G_SLOTS)
    config.channel_bitmap.ghz_2_channels = (uint16_t)(1u << (slot + 1));
  else
    config.channel_bitmap.ghz_5_channels = 1u << (slot - SPI_WIFI_SCAN_2G_SLOTS + 1);
  err = esp_wifi_scan_start(&config, true);
  if (err == ESP_OK) {
    uint16_t count = WIFI_SCAN_LIST_SIZE;
    err = esp_wifi_scan_get_ap_records(&count, s_stored_aps);
    if (err == ESP_OK)
      s_stored_ap_count = count;
  }
  esp_wifi_clear_ap_list();
cleanup:
  if (has_band) {
    esp_err_t restore_err = esp_wifi_set_band_mode(previous_band);
    if (err == ESP_OK)
      err = restore_err;
  }
  xSemaphoreGive(s_mutex);
  return err;
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
    s_promiscuous_active = true;
    ESP_LOGI(TAG, "Promiscuous mode enabled");
  }
}

void wifi_service_promiscuous_stop(void) {
  esp_err_t err = esp_wifi_set_promiscuous(false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to disable promiscuous mode: %s", esp_err_to_name(err));
  }

  s_promiscuous_active = false;
  esp_wifi_set_promiscuous_rx_cb(NULL);
  ESP_LOGI(TAG, "Promiscuous mode disabled");
}

bool wifi_service_is_busy(void) {
  return s_promiscuous_active;
}

void wifi_service_set_power_save(bool deep) {
  esp_wifi_set_ps(deep ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
}

esp_err_t wifi_service_set_capture_hopping(bool enabled) {
  wifi_service_hop_channel_t *channels = NULL;
  wifi_service_hop_ap_t *aps = NULL;
  if (enabled && s_hop_channels == NULL) {
    channels = heap_caps_calloc(
        CAPTURE_HOP_CHANNEL_CAPACITY, sizeof(*channels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (channels == NULL) {
      ESP_LOGE(TAG, "Failed to allocate capture channels in PSRAM");
      goto cleanup;
    }
    aps = heap_caps_calloc(
        CAPTURE_HOP_AP_CAPACITY, sizeof(*aps), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (aps == NULL) {
      ESP_LOGE(TAG, "Failed to allocate capture activity in PSRAM");
      goto cleanup;
    }
    // Configure only while stopped; retain bounded storage for reuse and RX lifetime safety.
    s_hop_channels = channels;
    s_hop_aps = aps;
  }
  portENTER_CRITICAL(&s_hop_lock);
  memset(&s_hop_state, 0, sizeof(s_hop_state));
  s_hop_state.is_capture = enabled;
  if (s_hop_channels != NULL)
    memset(s_hop_channels, 0, CAPTURE_HOP_CHANNEL_CAPACITY * sizeof(*s_hop_channels));
  if (s_hop_aps != NULL)
    memset(s_hop_aps, 0, CAPTURE_HOP_AP_CAPACITY * sizeof(*s_hop_aps));
  s_hop_channel_count = 0;
  memset(&s_capture_status, 0, sizeof(s_capture_status));
  s_capture_action = NULL;
  portEXIT_CRITICAL(&s_hop_lock);
  return ESP_OK;

cleanup:
  free(aps);
  free(channels);
  return ESP_ERR_NO_MEM;
}

static void decay_score(wifi_service_hop_channel_t *channel, int64_t now) {
  int64_t periods = (now - channel->decay_us) / CAPTURE_HOP_DECAY_US;
  if (periods <= 0)
    return;
  channel->score = periods >= CAPTURE_HOP_SCORE_BITS ? 0 : channel->score >> periods;
  channel->decay_us += periods * CAPTURE_HOP_DECAY_US;
}

static void credit_activity(uint8_t channel,
                            wifi_service_capture_activity_t activity,
                            const uint8_t *bssid,
                            int64_t now) {
  if (bssid == NULL || (bssid[0] & 1) != 0)
    return;
  static const uint8_t EMPTY_BSSID[CAPTURE_HOP_BSSID_LEN] = {0};
  if (memcmp(bssid, EMPTY_BSSID, sizeof(EMPTY_BSSID)) == 0)
    return;
  wifi_service_hop_channel_t *history = NULL;
  for (size_t i = 0; i < s_hop_channel_count; i++) {
    if (s_hop_channels[i].channel == channel) {
      history = &s_hop_channels[i];
      break;
    }
  }
  if (history == NULL)
    return;
  wifi_service_hop_ap_t *entry = NULL;
  wifi_service_hop_ap_t *oldest = &s_hop_aps[0];
  for (size_t i = 0; i < CAPTURE_HOP_AP_CAPACITY; i++) {
    wifi_service_hop_ap_t *candidate = &s_hop_aps[i];
    if (candidate->channel == channel &&
        memcmp(candidate->bssid, bssid, CAPTURE_HOP_BSSID_LEN) == 0) {
      entry = candidate;
      break;
    }
    if (candidate->channel == 0 || candidate->touched_us < oldest->touched_us)
      oldest = candidate;
  }
  if (entry == NULL) {
    entry = oldest;
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->bssid, bssid, CAPTURE_HOP_BSSID_LEN);
    entry->channel = channel;
  }
  entry->touched_us = now;
  int64_t interval =
      activity == WIFI_SERVICE_CAPTURE_AP ? CAPTURE_HOP_DECAY_US : CAPTURE_HOP_ACTIVITY_US;
  uint8_t mask = (uint8_t)(1U << activity);
  if ((entry->seen_mask & mask) != 0 && now - entry->credited_us[activity] < interval)
    return;
  entry->seen_mask |= mask;
  entry->credited_us[activity] = now;
  decay_score(history, now);
  uint16_t weight = activity == WIFI_SERVICE_CAPTURE_EAPOL   ? CAPTURE_HOP_EAPOL_WEIGHT
                    : activity == WIFI_SERVICE_CAPTURE_ASSOC ? CAPTURE_HOP_ASSOC_WEIGHT
                                                             : 1;
  uint16_t score = history->score + weight;
  history->score = score > CAPTURE_HOP_SCORE_MAX ? CAPTURE_HOP_SCORE_MAX : score;
}

static size_t select_hop(const uint8_t *channels, size_t count, size_t fallback) {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_hop_lock);
  if (!s_hop_state.is_capture || count == 0 || count > CAPTURE_HOP_CHANNEL_CAPACITY) {
    portEXIT_CRITICAL(&s_hop_lock);
    return fallback;
  }
  bool is_same_plan = count == s_hop_channel_count;
  for (size_t i = 0; is_same_plan && i < count; i++)
    is_same_plan = s_hop_channels[i].channel == channels[i];
  if (!is_same_plan) {
    memset(s_hop_channels, 0, CAPTURE_HOP_CHANNEL_CAPACITY * sizeof(*s_hop_channels));
    memset(s_hop_aps, 0, CAPTURE_HOP_AP_CAPACITY * sizeof(*s_hop_aps));
    s_hop_channel_count = count;
    for (size_t i = 0; i < count; i++) {
      s_hop_channels[i].channel = channels[i];
      s_hop_channels[i].decay_us = now;
    }
  }
  size_t oldest = 0;
  size_t best = 0;
  uint32_t best_score = 0;
  int64_t best_age = -1;
  for (size_t i = 0; i < count; i++) {
    wifi_service_hop_channel_t *item = &s_hop_channels[i];
    if (!item->is_visited) {
      oldest = i;
      break;
    }
    decay_score(item, now);
    if (item->visited_us < s_hop_channels[oldest].visited_us)
      oldest = i;
    int64_t age = now - item->visited_us;
    uint32_t age_bonus = (uint32_t)(age / CAPTURE_HOP_AGE_UNIT_US);
    if (age_bonus > CAPTURE_HOP_AGE_MAX)
      age_bonus = CAPTURE_HOP_AGE_MAX;
    uint32_t score = item->score + age_bonus;
    if (score > best_score || (score == best_score && age > best_age)) {
      best = i;
      best_score = score;
      best_age = age;
    }
  }
  size_t selected = !s_hop_channels[oldest].is_visited ||
                            now - s_hop_channels[oldest].visited_us >= CAPTURE_HOP_REVISIT_US
                        ? oldest
                        : best;
  s_hop_channels[selected].is_visited = true;
  s_hop_channels[selected].visited_us = now;
  portEXIT_CRITICAL(&s_hop_lock);
  return selected;
}

void wifi_service_observe_capture(uint8_t channel,
                                  wifi_service_capture_activity_t activity,
                                  const uint8_t *bssid) {
  if (activity < WIFI_SERVICE_CAPTURE_AP || activity >= WIFI_SERVICE_CAPTURE_ACTIVITY_COUNT)
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_hop_lock);
  if (s_hop_state.is_capture && channel != 0 && channel == s_hop_state.channel) {
    credit_activity(channel, activity, bssid, now);
    int64_t deadline = s_hop_state.deadline_us;
    if (activity == WIFI_SERVICE_CAPTURE_AP)
      deadline = s_hop_state.started_us + CAPTURE_HOP_AP_MS * CAPTURE_US_PER_MS;
    else if (activity == WIFI_SERVICE_CAPTURE_ASSOC)
      deadline = now + CAPTURE_HOP_ASSOC_MS * CAPTURE_US_PER_MS;
    else if (activity == WIFI_SERVICE_CAPTURE_EAPOL)
      deadline = now + CAPTURE_HOP_EAPOL_MS * CAPTURE_US_PER_MS;
    int64_t maximum = s_hop_state.started_us + CAPTURE_HOP_MAX_MS * CAPTURE_US_PER_MS;
    if (deadline > maximum)
      deadline = maximum;
    if (deadline > s_hop_state.deadline_us)
      s_hop_state.deadline_us = deadline;
  }
  portEXIT_CRITICAL(&s_hop_lock);
}

void wifi_service_set_capture_action(wifi_service_capture_action_cb_t action) {
  portENTER_CRITICAL(&s_hop_lock);
  s_capture_action = action;
  portEXIT_CRITICAL(&s_hop_lock);
}

void wifi_service_get_capture_status(wifi_service_capture_status_t *out_status) {
  if (out_status == NULL)
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_hop_lock);
  *out_status = s_capture_status;
  int64_t remaining = s_hop_state.deadline_us - now;
  out_status->hold_seconds =
      s_hop_state.has_ap_hold && s_hop_state.channel != 0 && remaining > 0
          ? (uint8_t)((remaining + CAPTURE_HOP_US_PER_SECOND - 1) / CAPTURE_HOP_US_PER_SECOND)
          : 0;
  portEXIT_CRITICAL(&s_hop_lock);
}

static uint16_t security_word(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << CAPTURE_HOP_BYTE_SHIFT);
}

static bool is_security_body_valid(const uint8_t *body, size_t length, bool is_rsn) {
  if (length < CAPTURE_HOP_SECURITY_WORD_LEN || security_word(body) != CAPTURE_HOP_SECURITY_VERSION)
    return false;
  body += CAPTURE_HOP_SECURITY_WORD_LEN;
  length -= CAPTURE_HOP_SECURITY_WORD_LEN;
  // Omitted trailing fields use protocol defaults; partially present fields are invalid.
  if (length == 0)
    return true;
  if (length < CAPTURE_HOP_SECURITY_SELECTOR_LEN)
    return false;
  body += CAPTURE_HOP_SECURITY_SELECTOR_LEN;
  length -= CAPTURE_HOP_SECURITY_SELECTOR_LEN;
  for (size_t i = 0; i < CAPTURE_HOP_SECURITY_LIST_COUNT; i++) {
    if (length == 0)
      return true;
    if (length < CAPTURE_HOP_SECURITY_WORD_LEN)
      return false;
    uint16_t count = security_word(body);
    body += CAPTURE_HOP_SECURITY_WORD_LEN;
    length -= CAPTURE_HOP_SECURITY_WORD_LEN;
    if (count == 0 || count > length / CAPTURE_HOP_SECURITY_SELECTOR_LEN)
      return false;
    size_t bytes = (size_t)count * CAPTURE_HOP_SECURITY_SELECTOR_LEN;
    body += bytes;
    length -= bytes;
  }
  if (length == 0)
    return true;
  if (length < CAPTURE_HOP_SECURITY_WORD_LEN)
    return false;
  body += CAPTURE_HOP_SECURITY_WORD_LEN;
  length -= CAPTURE_HOP_SECURITY_WORD_LEN;
  if (!is_rsn || length == 0)
    return length == 0;
  if (length < CAPTURE_HOP_SECURITY_WORD_LEN)
    return false;
  uint16_t pmkid_count = security_word(body);
  length -= CAPTURE_HOP_SECURITY_WORD_LEN;
  if (pmkid_count > length / CAPTURE_HOP_PMKID_LEN)
    return false;
  length -= (size_t)pmkid_count * CAPTURE_HOP_PMKID_LEN;
  return length == 0 || length == CAPTURE_HOP_SECURITY_SELECTOR_LEN;
}

static bool has_secured_ie(const uint8_t *ies, size_t length) {
  if (ies == NULL)
    return false;
  bool has_rsn = false;
  bool has_wpa = false;
  while (length != 0) {
    if (length < CAPTURE_HOP_IE_HEADER_LEN || ies[1] > length - CAPTURE_HOP_IE_HEADER_LEN)
      return false;
    uint8_t tag = ies[0];
    uint8_t size = ies[1];
    const uint8_t *body = ies + CAPTURE_HOP_IE_HEADER_LEN;
    if (tag == CAPTURE_HOP_RSN_TAG) {
      if (has_rsn || !is_security_body_valid(body, size, true))
        return false;
      has_rsn = true;
    } else if (tag == CAPTURE_HOP_VENDOR_TAG && size >= CAPTURE_HOP_WPA_OUI_LEN &&
               body[0] == CAPTURE_HOP_WPA_OUI_0 && body[1] == CAPTURE_HOP_WPA_OUI_1 &&
               body[2] == CAPTURE_HOP_WPA_OUI_2 && body[3] == CAPTURE_HOP_WPA_OUI_TYPE) {
      if (has_wpa || !is_security_body_valid(
                         body + CAPTURE_HOP_WPA_OUI_LEN, size - CAPTURE_HOP_WPA_OUI_LEN, false))
        return false;
      has_wpa = true;
    }
    ies += CAPTURE_HOP_IE_HEADER_LEN + size;
    length -= CAPTURE_HOP_IE_HEADER_LEN + size;
  }
  return has_rsn || has_wpa;
}

void wifi_service_mark_captured(const uint8_t *bssid) {
  static const uint8_t EMPTY_MAC[CAPTURE_HOP_BSSID_LEN] = {0};
  if (bssid == NULL || (bssid[0] & 1) != 0 || memcmp(bssid, EMPTY_MAC, sizeof(EMPTY_MAC)) == 0)
    return;
  portENTER_CRITICAL(&s_hop_lock);
  wifi_service_hop_lab_t *entry = NULL;
  wifi_service_hop_lab_t *empty = NULL;
  for (size_t i = 0; i < CAPTURE_HOP_LAB_CAPACITY; i++) {
    wifi_service_hop_lab_t *ap = &s_hop_labs[i];
    if (ap->attempts == 0 && !ap->is_captured) {
      if (empty == NULL)
        empty = ap;
    } else if (memcmp(ap->bssid, bssid, CAPTURE_HOP_BSSID_LEN) == 0) {
      entry = ap;
      break;
    }
  }
  if (entry == NULL)
    entry = empty;
  if (entry != NULL) {
    memcpy(entry->bssid, bssid, CAPTURE_HOP_BSSID_LEN);
    entry->is_captured = true;
  }
  if (s_hop_state.has_action_pending &&
      memcmp(s_hop_state.bssid, bssid, CAPTURE_HOP_BSSID_LEN) == 0)
    s_hop_state.has_action_pending = false;
  portEXIT_CRITICAL(&s_hop_lock);
}

void wifi_service_observe_capture_ap(uint8_t channel,
                                     const uint8_t *bssid,
                                     const uint8_t *ies,
                                     size_t length) {
  static const uint8_t EMPTY_MAC[CAPTURE_HOP_BSSID_LEN] = {0};
  if (bssid == NULL || (bssid[0] & 1) != 0 || memcmp(bssid, EMPTY_MAC, sizeof(EMPTY_MAC)) == 0 ||
      !has_secured_ie(ies, length))
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_hop_lock);
  if (!s_hop_state.is_capture || s_capture_action == NULL || channel == 0 ||
      channel != s_hop_state.channel || s_hop_state.has_ap_hold || now >= s_hop_state.deadline_us)
    goto cleanup;
  wifi_service_hop_lab_t *entry = NULL;
  wifi_service_hop_lab_t *empty = NULL;
  for (size_t i = 0; i < CAPTURE_HOP_LAB_CAPACITY; i++) {
    wifi_service_hop_lab_t *ap = &s_hop_labs[i];
    if (ap->attempts == 0 && !ap->is_captured) {
      if (empty == NULL)
        empty = ap;
    } else if (memcmp(ap->bssid, bssid, CAPTURE_HOP_BSSID_LEN) == 0) {
      entry = ap;
      break;
    }
  }
  if (entry == NULL)
    entry = empty;
  if (entry == NULL || entry->is_captured || entry->attempts >= CAPTURE_HOP_LAB_ATTEMPTS)
    goto cleanup;
  memcpy(entry->bssid, bssid, CAPTURE_HOP_BSSID_LEN);
  entry->attempts++;
  s_capture_status.ap_attempt = entry->attempts;
  s_hop_state.has_ap_hold = true;
  s_hop_state.has_action_pending = true;
  memcpy(s_hop_state.bssid, bssid, CAPTURE_HOP_BSSID_LEN);
  s_hop_state.deadline_us = now + CAPTURE_HOP_LAB_HOLD_US;
cleanup:
  portEXIT_CRITICAL(&s_hop_lock);
}

// Caller serializes this task-context operation with hopper stop.
static void process_capture_action(void) {
  uint8_t bssid[CAPTURE_HOP_BSSID_LEN];
  uint8_t channel = 0;
  wifi_service_capture_action_cb_t action = NULL;
  portENTER_CRITICAL(&s_hop_lock);
  if (s_hop_state.is_capture && s_hop_state.has_action_pending) {
    action = s_capture_action;
    channel = s_hop_state.channel;
    memcpy(bssid, s_hop_state.bssid, sizeof(bssid));
    s_hop_state.has_action_pending = false;
  }
  portEXIT_CRITICAL(&s_hop_lock);
  if (action == NULL || channel == 0)
    return;
  esp_err_t err = action(bssid, channel);
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_hop_lock);
  s_capture_status.attempts++;
  if (err != ESP_OK)
    s_capture_status.failures++;
  s_hop_state.deadline_us = now + CAPTURE_HOP_LAB_HOLD_US;
  portEXIT_CRITICAL(&s_hop_lock);
  if (err == ESP_OK)
    ESP_LOGI(TAG, "Capture TX accepted on channel %u; holding for 15 seconds", channel);
  else
    ESP_LOGW(TAG, "Capture TX failed on channel %u: %s", channel, esp_err_to_name(err));
}

static void begin_hop(uint8_t channel) {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_hop_lock);
  s_hop_state.has_ap_hold = false;
  s_hop_state.has_action_pending = false;
  s_hop_state.channel = channel;
  s_hop_state.started_us = now;
  s_hop_state.deadline_us =
      now + (s_hop_state.is_capture ? CAPTURE_HOP_EMPTY_MS : HOPPER_DELAY_MS) * CAPTURE_US_PER_MS;
  portEXIT_CRITICAL(&s_hop_lock);
}

static bool finish_hop(void) {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_hop_lock);
  bool is_finished = now >= s_hop_state.deadline_us;
  if (is_finished)
    s_hop_state.channel = 0;
  portEXIT_CRITICAL(&s_hop_lock);
  return is_finished;
}

void wifi_service_start_channel_hopping(void) {
  if (s_hopper_task_handle != NULL) {
    ESP_LOGW(TAG, "Channel hopping already running");
    return;
  }

  if (s_hopper_action_mutex == NULL) {
    s_hopper_action_mutex = xSemaphoreCreateMutexStatic(&s_hopper_action_storage);
    if (s_hopper_action_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create hopper action mutex");
      return;
    }
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

    xTaskCreatePinnedToCore(channel_hopper_task,
                            "chan_hopper_srv",
                            HOPPER_STACK_SIZE,
                            NULL,
                            SYS_PRIO_SERVICE_HI,
                            &s_hopper_task_handle,
                            SYS_CORE_MAIN);
  } else {
    s_hopper_task_handle = xTaskCreateStaticPinnedToCore(channel_hopper_task,
                                                         "chan_hopper_srv",
                                                         HOPPER_STACK_SIZE,
                                                         NULL,
                                                         SYS_PRIO_SERVICE_HI,
                                                         s_hopper_task_stack,
                                                         s_hopper_task_tcb,
                                                         SYS_CORE_MAIN);
  }

  if (s_hopper_task_handle != NULL) {
    ESP_LOGI(TAG, "Channel hopping started");
  } else {
    ESP_LOGE(TAG, "Failed to start channel hopping task");
  }
}

void wifi_service_stop_channel_hopping(void) {
  if (s_hopper_action_mutex != NULL)
    xSemaphoreTake(s_hopper_action_mutex, portMAX_DELAY);
  wifi_service_set_capture_hopping(false);
  if (s_hopper_task_handle != NULL) {
    vTaskDelete(s_hopper_task_handle);
    s_hopper_task_handle = NULL;
  }

  free(s_hopper_task_stack);
  s_hopper_task_stack = NULL;

  free(s_hopper_task_tcb);
  s_hopper_task_tcb = NULL;

  if (s_hopper_action_mutex != NULL)
    xSemaphoreGive(s_hopper_action_mutex);
  ESP_LOGI(TAG, "Channel hopping stopped");
}

static void
event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "Station connected to AP, MAC: " MACSTR, MAC2STR(event->mac));
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    ESP_LOGI(TAG, "Station disconnected from AP");
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected from AP");
    s_is_connected = false;
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
    ESP_LOGI(TAG, "IP assigned to station connected to AP");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Got IP address, Wi-Fi connected");
    s_is_connected = true;
  }
}

static const uint8_t HOP_CHANNELS_2G[] = {1, 6, 11, 3, 9, 13, 2, 4, 5, 7, 8, 10, 12};
static const uint8_t HOP_CHANNELS_5G[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
static const uint8_t HOP_CHANNELS_DUAL[] = {1,   36, 6,   40, 11,  44, 3,   48, 9, 149, 13,
                                            153, 2,  157, 4,  161, 5,  165, 7,  8, 10,  12};
static const uint8_t CAPTURE_CHANNELS_5G[] = {
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116, 120,
    124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177,
};
#define CAPTURE_CHANNELS_5G_COUNT (sizeof(CAPTURE_CHANNELS_5G) / sizeof(CAPTURE_CHANNELS_5G[0]))

esp_err_t wifi_service_capture_passive(const wifi_service_capture_config_t *config,
                                       wifi_promiscuous_cb_t rx_cb) {
  if (config == NULL || rx_cb == NULL || config->tick_cb == NULL || config->duration_ms == 0 ||
      config->band > CAPTURE_BAND_5G) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mutex == NULL || xSemaphoreTake(s_mutex, 0) != pdTRUE) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t err = ESP_ERR_INVALID_STATE;
  wifi_mode_t previous_mode;
  wifi_band_mode_t previous_band;
  wifi_promiscuous_filter_t previous_filter;
  bool is_started = false;
  bool has_mode = false;
  bool has_filter = false;
  bool is_promiscuous = false;
  uint8_t channels[CAPTURE_CHANNEL_2G_MAX + CAPTURE_CHANNELS_5G_COUNT];
  size_t channel_count = 0;
  size_t index = 0;
  const char *stage = "ownership";
  if (s_is_active || s_is_connected || s_promiscuous_active || s_hopper_task_handle != NULL) {
    goto cleanup;
  }
  stage = "read mode";
  if ((err = esp_wifi_get_mode(&previous_mode)) != ESP_OK) {
    goto cleanup;
  }
  has_mode = true;
  stage = "read band";
  if ((err = esp_wifi_get_band_mode(&previous_band)) != ESP_OK) {
    goto cleanup;
  }
  stage = "read promiscuous state";
  if ((err = esp_wifi_get_promiscuous(&is_promiscuous)) != ESP_OK || is_promiscuous) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  stage = "read filter";
  if ((err = esp_wifi_get_promiscuous_filter(&previous_filter)) != ESP_OK) {
    goto cleanup;
  }
  has_filter = true;
  wifi_country_t country;
  stage = "read country";
  if ((err = esp_wifi_get_country(&country)) != ESP_OK) {
    goto cleanup;
  }
  if (config->band != CAPTURE_BAND_5G) {
    for (size_t i = 0; i <= sizeof(HOP_CHANNELS_2G); i++) {
      uint8_t channel = i < sizeof(HOP_CHANNELS_2G) ? HOP_CHANNELS_2G[i] : CAPTURE_CHANNEL_2G_MAX;
      if (channel >= country.schan && (uint16_t)channel < (uint16_t)country.schan + country.nchan) {
        channels[channel_count++] = channel;
      }
    }
  }
  if (config->band != CAPTURE_BAND_2G) {
    for (size_t i = 0; i < CAPTURE_CHANNELS_5G_COUNT; i++) {
      if (country.policy != WIFI_COUNTRY_POLICY_MANUAL || country.wifi_5g_channel_mask == 0 ||
          (country.wifi_5g_channel_mask & (1u << (i + 1))) != 0) {
        channels[channel_count++] = CAPTURE_CHANNELS_5G[i];
      }
    }
  }
  stage = "set NULL mode";
  if ((err = esp_wifi_set_mode(WIFI_MODE_NULL)) != ESP_OK) {
    goto cleanup;
  }
  stage = "start driver";
  if ((err = esp_wifi_start()) != ESP_OK) {
    goto cleanup;
  }
  is_started = true;
  // Band selection requires a started driver, including NULL-mode captures.
  stage = "select band";
  if ((err = esp_wifi_set_band_mode(band_mode_from_selector(config->band))) != ESP_OK) {
    goto cleanup;
  }
  wifi_promiscuous_filter_t filter = {
      .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA |
                     WIFI_PROMIS_FILTER_MASK_CTRL,
  };
  stage = "configure capture";
  if ((err = esp_wifi_set_promiscuous_filter(&filter)) != ESP_OK ||
      (err = esp_wifi_set_promiscuous_rx_cb(rx_cb)) != ESP_OK) {
    goto cleanup;
  }
  // Unsupported/regulatory-disallowed channels never consume a capture dwell.
  stage = "tune initial channel";
  for (; index < channel_count; index++) {
    if (esp_wifi_set_channel(channels[index], WIFI_SECOND_CHAN_NONE) == ESP_OK) {
      break;
    }
  }
  if (index == channel_count) {
    err = ESP_ERR_NOT_SUPPORTED;
    goto cleanup;
  }
  stage = "enable promiscuous mode";
  if ((err = esp_wifi_set_promiscuous(true)) != ESP_OK) {
    goto cleanup;
  }
  int64_t start_us = esp_timer_get_time();
  stage = "capture channels";
  while ((uint64_t)(esp_timer_get_time() - start_us) / CAPTURE_US_PER_MS < config->duration_ms) {
    vTaskDelay(pdMS_TO_TICKS(HOPPER_DELAY_MS));
    if (!config->tick_cb(config->ctx)) {
      break;
    }
    size_t attempts = 0;
    do {
      index = (index + 1) % channel_count;
      err = esp_wifi_set_channel(channels[index], WIFI_SECOND_CHAN_NONE);
      attempts++;
    } while (err != ESP_OK && attempts < channel_count);
    if (err != ESP_OK) {
      break;
    }
  }
cleanup:
  if (is_started) {
    esp_err_t cleanup_err = esp_wifi_set_promiscuous(false);
    if (err == ESP_OK) {
      err = cleanup_err;
    }
    cleanup_err = esp_wifi_set_promiscuous_rx_cb(NULL);
    if (err == ESP_OK) {
      err = cleanup_err;
    }
    cleanup_err = esp_wifi_set_band_mode(previous_band);
    if (err == ESP_OK && cleanup_err != ESP_OK) {
      stage = "restore band";
      err = cleanup_err;
    }
    cleanup_err = esp_wifi_stop();
    if (cleanup_err != ESP_OK) {
      // Keep NULL mode if stopping fails; restoring AP/STA could transmit.
      has_mode = false;
      s_is_active = true;
    }
    if (err == ESP_OK) {
      err = cleanup_err;
    }
  }
  if (has_filter) {
    esp_err_t cleanup_err = esp_wifi_set_promiscuous_filter(&previous_filter);
    if (err == ESP_OK) {
      err = cleanup_err;
    }
  }
  if (has_mode) {
    esp_err_t cleanup_err = esp_wifi_set_mode(previous_mode);
    if (err == ESP_OK) {
      err = cleanup_err;
    }
  }
  xSemaphoreGive(s_mutex);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Passive capture ended at %s: %s", stage, esp_err_to_name(err));
  }
  return err;
}

static void channel_hopper_task(void *pvParameters) {
  (void)pvParameters;
  size_t idx = 0;
  while (1) {
    const uint8_t *channels = HOP_CHANNELS_DUAL;
    size_t count = sizeof(HOP_CHANNELS_DUAL) / sizeof(HOP_CHANNELS_DUAL[0]);
    if (s_analysis_band == 1) {
      channels = HOP_CHANNELS_2G;
      count = sizeof(HOP_CHANNELS_2G) / sizeof(HOP_CHANNELS_2G[0]);
    } else if (s_analysis_band == 2) {
      channels = HOP_CHANNELS_5G;
      count = sizeof(HOP_CHANNELS_5G) / sizeof(HOP_CHANNELS_5G[0]);
    }
    if (idx >= count)
      idx = 0;
    idx = select_hop(channels, count, idx);
    esp_err_t err = esp_wifi_set_channel(channels[idx], WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK)
      ESP_LOGD(TAG, "Skip unsupported channel %u: %s", channels[idx], esp_err_to_name(err));
    begin_hop(err == ESP_OK ? channels[idx] : 0);
    idx++;
    if (idx >= count) {
      idx = 0;
    }
    do {
      vTaskDelay(pdMS_TO_TICKS(CAPTURE_HOP_POLL_MS));
      xSemaphoreTake(s_hopper_action_mutex, portMAX_DELAY);
      process_capture_action();
      xSemaphoreGive(s_hopper_action_mutex);
    } while (!finish_hop());
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

#include "hb_muse_wifi_guard.inc"
#include "hb_muse_wifi_scan.inc"
