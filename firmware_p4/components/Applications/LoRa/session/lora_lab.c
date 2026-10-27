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

#include "lora_lab.h"

#include <limits.h>
#include <string.h>
#include <stdatomic.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "lora_session.h"
#include "pin_def.h"
#include "power_manager.h"
#include "sx1262.h"
#include "sx1262_hal.h"
#include "sx1262_regs.h"
#include "sys_prio.h"

#define LORA_LAB_TASK_STACK   8192
#define LORA_LAB_SAMPLE_MS    50
#define LORA_LAB_STOP_WAIT_MS 2500
#define LORA_LAB_TX_POWER_DBM (-9)

static const char *TAG = "LORA_LAB";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static lora_lab_snapshot_t s_snapshot = {0};
static sx1262_config_t s_config = {0};
static atomic_bool s_stop_requested = false;
static _Atomic(TaskHandle_t) s_task;
static bool s_no_sleep_held = false;
static int64_t s_rssi_sum = 0;

static void snapshot_set_failure(esp_err_t error) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot.state = LORA_LAB_FAILED;
  s_snapshot.error = error;
  portEXIT_CRITICAL(&s_lock);
}

static esp_err_t lab_cleanup(void) {
  if (sx1262_is_running()) {
    sx1262_stop_rx();
  }
  esp_err_t error = sx1262_deinit();
  if (error != ESP_OK)
    return error;
  if (s_no_sleep_held) {
    error = power_manager_no_sleep_release();
    if (error != ESP_OK)
      return error;
    s_no_sleep_held = false;
  }
  memset(&s_config, 0, sizeof(s_config));
  return lora_session_radio_release(&s_config);
}

static void on_rx_done(const sx1262_packet_t *packet, void *ctx) {
  (void)ctx;
  if (packet == NULL) {
    return;
  }

  portENTER_CRITICAL(&s_lock);
  if (!packet->has_crc_error && !packet->has_header_error && packet->len > 0) {
    s_snapshot.rx_packets++;
    s_snapshot.last_packet_rssi_dbm = packet->rssi_pkt_dbm;
    s_snapshot.last_packet_snr_db = packet->snr_pkt_db;
    s_snapshot.last_packet_len = packet->len;
    size_t prefix_len = packet->len;
    if (prefix_len > sizeof(s_snapshot.last_packet_prefix)) {
      prefix_len = sizeof(s_snapshot.last_packet_prefix);
    }
    memcpy(s_snapshot.last_packet_prefix, packet->buf, prefix_len);
    if (prefix_len < sizeof(s_snapshot.last_packet_prefix)) {
      memset(s_snapshot.last_packet_prefix + prefix_len,
             0,
             sizeof(s_snapshot.last_packet_prefix) - prefix_len);
    }
  }
  portEXIT_CRITICAL(&s_lock);

  /* RX_DONE returns the chip to standby. Re-arm only while the lab still owns it. */
  if (!s_stop_requested) {
    esp_err_t err = sx1262_receive_continuous();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "RX re-arm failed: %s", esp_err_to_name(err));
    }
  }
}

static void on_radio_error(int error, void *ctx) {
  (void)ctx;
  ESP_LOGW(TAG, "radio IRQ error: %d", error);
}

static esp_err_t lab_radio_begin(uint32_t frequency_hz) {
  esp_err_t err = power_manager_no_sleep_acquire();
  if (err != ESP_OK) {
    return err;
  }
  s_no_sleep_held = true;

  err = sx1262_hal_create(&s_config.hal);
  if (err != ESP_OK) {
    return err;
  }

  s_config.frequency_hz = frequency_hz;
  s_config.sf = SX1262_LORA_SF7;
  s_config.bw = SX1262_LORA_BW_125;
  s_config.cr = SX1262_LORA_CR_4_5;
  s_config.tx_power_dbm = LORA_LAB_TX_POWER_DBM;
  s_config.preamble_len = 12;
  s_config.is_crc_on = true;
  s_config.is_inverted_iq = false;
  s_config.is_implicit_hdr = false;
  s_config.is_public_network = false;

  err = sx1262_init(&s_config);
  if (err != ESP_OK) {
    return err;
  }

  sx1262_callbacks_t callbacks = {
      .on_rx_done = on_rx_done,
      .on_error = on_radio_error,
      .cb_ctx = NULL,
  };
  err = sx1262_set_callbacks(&callbacks);
  if (err != ESP_OK) {
    return err;
  }
  return sx1262_start();
}

static bool chip_status_valid(uint8_t status, uint16_t errors) {
  uint8_t mode = (status & SX1262_STATUS_CHIP_MODE_MASK) >> SX1262_STATUS_CHIP_MODE_SHIFT;
  return status != 0x00 && status != 0xFF && errors == 0 &&
         (mode == SX1262_CHIP_MODE_STDBY_RC || mode == SX1262_CHIP_MODE_STDBY_XOSC);
}

static void lab_task(void *ctx) {
  /* xTaskCreate may schedule this task before lora_lab_start stores the handle. */
  while (s_task == NULL) {
    vTaskDelay(1);
  }
  uint32_t frequency_hz = (uint32_t)(uintptr_t)ctx;
  esp_err_t err = lab_radio_begin(frequency_hz);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "bring-up failed: %s", esp_err_to_name(err));
    snapshot_set_failure(err);
    while (lab_cleanup() != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(LORA_LAB_SAMPLE_MS));
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  uint8_t status = 0;
  uint16_t device_errors = 0;
  err = sx1262_get_status(&status);
  if (err == ESP_OK) {
    err = sx1262_get_device_errors(&device_errors);
  }
  if (err != ESP_OK || !chip_status_valid(status, device_errors)) {
    ESP_LOGE(TAG, "chip validation failed status=0x%02X errors=0x%04X", status, device_errors);
    snapshot_set_failure((err != ESP_OK) ? err : ESP_FAIL);
    while (lab_cleanup() != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(LORA_LAB_SAMPLE_MS));
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  err = sx1262_receive_continuous();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RX start failed: %s", esp_err_to_name(err));
    snapshot_set_failure(err);
    while (lab_cleanup() != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(LORA_LAB_SAMPLE_MS));
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  portENTER_CRITICAL(&s_lock);
  s_snapshot.state = LORA_LAB_RX;
  s_snapshot.error = ESP_OK;
  s_snapshot.chip_status = status;
  s_snapshot.device_errors = device_errors;
  portEXIT_CRITICAL(&s_lock);

  while (!s_stop_requested) {
    int16_t rssi = 0;
    if (sx1262_get_rssi_inst(&rssi) == ESP_OK) {
      portENTER_CRITICAL(&s_lock);
      s_snapshot.rssi_now_dbm = rssi;
      if (s_snapshot.rssi_samples == 0 || rssi < s_snapshot.rssi_min_dbm) {
        s_snapshot.rssi_min_dbm = rssi;
      }
      if (s_snapshot.rssi_samples == 0 || rssi > s_snapshot.rssi_max_dbm) {
        s_snapshot.rssi_max_dbm = rssi;
      }
      s_rssi_sum += rssi;
      s_snapshot.rssi_samples++;
      s_snapshot.rssi_avg_dbm = (int16_t)(s_rssi_sum / s_snapshot.rssi_samples);
      s_snapshot.busy_level = gpio_get_level(GPIO_LORA_BUSY_PIN);
      s_snapshot.dio1_level = gpio_get_level(GPIO_LORA_DIO1_PIN);
      portEXIT_CRITICAL(&s_lock);
    }

    sx1262_stats_t stats = {0};
    if (sx1262_get_stats(&stats) == ESP_OK) {
      portENTER_CRITICAL(&s_lock);
      s_snapshot.crc_errors = stats.nb_crc_error;
      s_snapshot.header_errors = stats.nb_header_error;
      portEXIT_CRITICAL(&s_lock);
    }
    vTaskDelay(pdMS_TO_TICKS(LORA_LAB_SAMPLE_MS));
  }

  while (lab_cleanup() != ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(LORA_LAB_SAMPLE_MS));
  portENTER_CRITICAL(&s_lock);
  s_snapshot.state = LORA_LAB_IDLE;
  s_snapshot.error = ESP_OK;
  portEXIT_CRITICAL(&s_lock);
  s_task = NULL;
  vTaskDelete(NULL);
}

esp_err_t lora_lab_start(uint32_t frequency_hz) {
  if (frequency_hz < 150000000U || frequency_hz > 960000000U) {
    return ESP_ERR_INVALID_ARG;
  }
  if (lora_session_active() != LORA_PROTO_NONE || s_task != NULL || sx1262_is_running()) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t error = lora_session_radio_reserve(&s_config);
  if (error != ESP_OK)
    return error;
  s_stop_requested = false;
  portENTER_CRITICAL(&s_lock);
  memset(&s_snapshot, 0, sizeof(s_snapshot));
  s_snapshot.state = LORA_LAB_STARTING;
  s_snapshot.frequency_hz = frequency_hz;
  s_snapshot.rssi_min_dbm = INT16_MAX;
  s_snapshot.rssi_max_dbm = INT16_MIN;
  s_rssi_sum = 0;
  portEXIT_CRITICAL(&s_lock);

  TaskHandle_t task = NULL;
  if (xTaskCreatePinnedToCore(lab_task,
                              "lora_lab",
                              LORA_LAB_TASK_STACK,
                              (void *)(uintptr_t)frequency_hz,
                              SYS_PRIO_BACKGROUND,
                              &task,
                              SYS_CORE_RADIO) != pdPASS) {
    (void)lora_session_radio_release(&s_config);
    snapshot_set_failure(ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
  }
  s_task = task;
  return ESP_OK;
}

void lora_lab_stop(void) {
  TaskHandle_t task = (TaskHandle_t)s_task;
  if (task == NULL) {
    return;
  }
  s_stop_requested = true;
  uint32_t waited_ms = 0;
  while (s_task != NULL && waited_ms < LORA_LAB_STOP_WAIT_MS) {
    vTaskDelay(pdMS_TO_TICKS(10));
    waited_ms += 10;
  }
  if (s_task != NULL) {
    ESP_LOGE(TAG, "worker did not stop within %u ms", LORA_LAB_STOP_WAIT_MS);
  }
}

bool lora_lab_active(void) {
  return s_task != NULL;
}

void lora_lab_get_snapshot(lora_lab_snapshot_t *out) {
  if (out == NULL) {
    return;
  }
  portENTER_CRITICAL(&s_lock);
  *out = s_snapshot;
  portEXIT_CRITICAL(&s_lock);
}

void lora_lab_reset_stats(void) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot.rssi_now_dbm = 0;
  s_snapshot.rssi_min_dbm = INT16_MAX;
  s_snapshot.rssi_max_dbm = INT16_MIN;
  s_snapshot.rssi_avg_dbm = 0;
  s_snapshot.rssi_samples = 0;
  s_snapshot.rx_packets = 0;
  s_snapshot.crc_errors = 0;
  s_snapshot.header_errors = 0;
  s_snapshot.last_packet_rssi_dbm = 0;
  s_snapshot.last_packet_snr_db = 0;
  s_snapshot.last_packet_len = 0;
  memset(s_snapshot.last_packet_prefix, 0, sizeof(s_snapshot.last_packet_prefix));
  s_rssi_sum = 0;
  portEXIT_CRITICAL(&s_lock);
}
