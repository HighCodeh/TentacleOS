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

#include "bridge_manager.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "board_service.h"
#include "c5_flasher.h"
#include "led_control.h"
#include "ota_version.h"
#include "spi_bridge.h"
#include "spi_session.h"
#include "sys_prio.h"

#define VERSION_BUF_SIZE       32
#define C5_MONITOR_PERIOD_MS   1500
#define C5_PROBE_TIMEOUT_MS    500
#define C5_DRAIN_TIMEOUT_MS    500
#define C5_BOOT_WAIT_MS        2000
#define C5_RESET_INTERVAL_MS   30000
#define C5_FAILURE_LIMIT       3
#define C5_RESET_LIMIT         2
#define C5_MONITOR_STACK_BYTES 6144
#define C5_BOARD_RETRY_MS      50
#define C5_BOARD_RETRIES       10
#define C5_US_PER_MS           1000

static const char *TAG = "BRIDGE_MGR";
static TaskHandle_t s_monitor;
static uint8_t s_failures;
static uint8_t s_resets;
static int64_t s_next_reset_us;

static esp_err_t check_c5_protocol(void) {
  spi_header_t header = {0};
  uint8_t reply[sizeof(uint16_t) + 1] = {0};
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_PROTO_VERSION, NULL, 0, &header, reply, sizeof(reply), C5_PROBE_TIMEOUT_MS);
  if (ret != ESP_OK)
    return ret;
  uint16_t version;
  memcpy(&version, reply, sizeof(version));
  if (header.length != sizeof(version) || version != SPI_PROTOCOL_VERSION) {
    ESP_LOGE(TAG, "C5 protocol mismatch; update the firmware pair");
    return ESP_ERR_NOT_SUPPORTED;
  }
  return ESP_OK;
}

static esp_err_t establish_link(void) {
  esp_err_t ret = check_c5_protocol();
  if (ret != ESP_OK)
    return ret;
  for (uint8_t attempt = 0; attempt < C5_BOARD_RETRIES; attempt++) {
    ret = board_service_reconcile();
    if (ret != ESP_ERR_INVALID_STATE)
      break;
    vTaskDelay(pdMS_TO_TICKS(C5_BOARD_RETRY_MS));
  }
  return ret;
}

static void restore_link(bool reset) {
  esp_err_t ret = spi_bridge_recovery_begin(C5_DRAIN_TIMEOUT_MS);
  if (ret != ESP_OK)
    return; // No drain confirmation: no GPIO reset.
  spi_session_link_lost();
  if (spi_bridge_recovery_is_disabled())
    ret = ESP_ERR_INVALID_STATE;
  if (reset && ret == ESP_OK) {
    // Budget belongs to P4 uptime, never to C5 epochs or successful probes.
    ++s_resets;
    s_next_reset_us = esp_timer_get_time() + (int64_t)C5_RESET_INTERVAL_MS * C5_US_PER_MS;
    ESP_LOGW(TAG, "Physical C5 reset %u/%u", s_resets, C5_RESET_LIMIT);
    ret = board_pins_reset_c5();
    if (ret == ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(C5_BOOT_WAIT_MS));
  }
  if (ret == ESP_OK)
    ret = establish_link();
  spi_bridge_recovery_finish(ret == ESP_OK);
  s_failures = 0;
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "C5 link negotiated; previous operations discarded");
    led_signal_info();
  } else {
    ESP_LOGW(TAG, "C5 recovery incomplete: %s", esp_err_to_name(ret));
    led_signal_error();
  }
}

static void monitor_once(void) {
  if (!spi_bridge_recovery_permitted()) {
    s_failures = 0;
    return;
  }
  spi_header_t header = {0};
  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_SYSTEM_PING, NULL, 0, &header, NULL, 0, C5_PROBE_TIMEOUT_MS);
  if (ret == ESP_OK && header.length == 0) {
    s_failures = 0;
    if (!spi_bridge_is_alive())
      restore_link(false);
    return;
  }
  if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT && ret != ESP_ERR_INVALID_RESPONSE &&
      ret != ESP_ERR_INVALID_CRC && ret != ESP_FAIL) {
    s_failures = 0;
    return;
  }
  if (s_failures < C5_FAILURE_LIMIT)
    ++s_failures;
  if (s_failures < C5_FAILURE_LIMIT)
    return;
  spi_bridge_set_alive(false);
  if (s_resets >= C5_RESET_LIMIT || esp_timer_get_time() < s_next_reset_us)
    return;
  restore_link(true);
}

static void c5_link_monitor(void *arg) {
  (void)arg;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(C5_MONITOR_PERIOD_MS));
    monitor_once();
  }
}

esp_err_t bridge_manager_init(void) {
  if (s_monitor != NULL)
    return ESP_OK;
  esp_err_t ret = spi_bridge_master_init_mode(SPI_BRIDGE_MODE_POLL);
  if (ret != ESP_OK)
    return ret;
  ret = board_service_init();
  if (ret != ESP_OK)
    return ret;
  spi_header_t header = {0};
  uint8_t version[VERSION_BUF_SIZE] = {0};
  ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_VERSION, NULL, 0, &header, version, sizeof(version) - 1, C5_PROBE_TIMEOUT_MS);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "C5 version: %s (expected %s)", version, FIRMWARE_VERSION);
    ret = check_c5_protocol();
  }
  spi_bridge_set_alive(ret == ESP_OK);
  if (xTaskCreatePinnedToCore(c5_link_monitor,
                              "c5_link_mon",
                              C5_MONITOR_STACK_BYTES,
                              NULL,
                              SYS_PRIO_SERVICE_LO,
                              &s_monitor,
                              SYS_CORE_RADIO) != pdPASS) {
    ESP_LOGE(TAG, "C5 supervisor allocation failed");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t bridge_manager_force_update(void) {
  esp_err_t ret = c5_flasher_init();
  if (ret != ESP_OK)
    return ret;
  return c5_flasher_update(NULL, 0, SPI_OTA_TRANSPORT_SPI);
}
