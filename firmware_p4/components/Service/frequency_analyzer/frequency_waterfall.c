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

#include "frequency_waterfall.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cc1101.h"
#include "cc1101_rf_path.h"
#include "led_control.h"
#include "resource_mgr.h"
#include "sys_prio.h"

#define WORKER_STACK       4096
#define PAUSE_POLL_MS      20
#define CLEANUP_RETRY_MS   100
#define AGC_TARGET         0x07U
#define AGC_CARRIER        0x08U
#define AGC_SAMPLES        0x30U
#define MANUAL_CALIBRATION 0x08U
#define SYMBOL_RATE        0x7FU
static const char *TAG = "FREQUENCY_WATERFALL";
typedef enum {
  FREQUENCY_WATERFALL_IDLE,
  FREQUENCY_WATERFALL_RUNNING,
  FREQUENCY_WATERFALL_STOPPING
} frequency_waterfall_state_t;
static atomic_uint s_state;
static atomic_bool s_pause;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static frequency_waterfall_snapshot_t s_snapshot;
static res_handle_t s_grant;
static void worker(void *context);

esp_err_t frequency_waterfall_start(const frequency_waterfall_config_t *config) {
  esp_err_t error = frequency_waterfall_validate(config);
  if (error != ESP_OK)
    return error;
  unsigned expected = FREQUENCY_WATERFALL_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, FREQUENCY_WATERFALL_RUNNING))
    return ESP_ERR_INVALID_STATE;
  res_request_t request = {
      .id = RES_SUBGHZ, .lane = RES_LANE_MAIN, .owner_kind = RES_OWNER_UI, .allow_preempt = false};
  error = resource_acquire(&request, &s_grant);
  if (error != ESP_OK)
    goto cleanup;
  portENTER_CRITICAL(&s_lock);
  s_snapshot = (frequency_waterfall_snapshot_t){.config = *config};
  portEXIT_CRITICAL(&s_lock);
  atomic_store(&s_pause, false);
  if (xTaskCreatePinnedToCore(worker,
                              "rf_waterfall",
                              WORKER_STACK,
                              NULL,
                              SYS_PRIO_BACKGROUND_LO,
                              NULL,
                              SYS_CORE_RADIO) == pdPASS)
    return ESP_OK;
  error = ESP_ERR_NO_MEM;
  resource_release(s_grant);
cleanup:
  atomic_store(&s_state, FREQUENCY_WATERFALL_IDLE);
  return error;
}
void frequency_waterfall_stop(void) {
  unsigned expected = FREQUENCY_WATERFALL_RUNNING;
  atomic_compare_exchange_strong(&s_state, &expected, FREQUENCY_WATERFALL_STOPPING);
}
bool frequency_waterfall_is_running(void) {
  return atomic_load(&s_state) != FREQUENCY_WATERFALL_IDLE;
}
void frequency_waterfall_pause(bool is_paused) {
  atomic_store(&s_pause, is_paused);
}
void frequency_waterfall_get_snapshot(frequency_waterfall_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out_snapshot = s_snapshot;
  portEXIT_CRITICAL(&s_lock);
}
static void record_error(esp_err_t error) {
  portENTER_CRITICAL(&s_lock);
  bool is_first = s_snapshot.error == ESP_OK;
  if (is_first)
    s_snapshot.error = error;
  portEXIT_CRITICAL(&s_lock);
  if (is_first) {
    ESP_LOGE(TAG, "Sweep failed: %s", esp_err_to_name(error));
    led_signal_error();
  }
}
static void worker(void *context) {
  (void)context;
  frequency_waterfall_snapshot_t line;
  frequency_waterfall_get_snapshot(&line);
  esp_err_t error = ESP_OK;
  bool has_radio = false;
  if (atomic_load(&s_state) != FREQUENCY_WATERFALL_RUNNING)
    goto cleanup;
  error = cc1101_init();
  if (error != ESP_OK)
    goto cleanup;
  has_radio = true;
  error = cc1101_set_preset(CC1101_PRESET_OOK_650KHZ, line.config.center_hz);
  if (error == ESP_OK)
    error = cc1101_strobe(CC1101_SIDLE);
  if (error == ESP_OK)
    error = cc1101_set_rx_bandwidth(frequency_waterfall_bandwidth(line.config.bandwidth));
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_MDMCFG3, SYMBOL_RATE);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_AGCCTRL2, AGC_TARGET);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_AGCCTRL1, AGC_CARRIER);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_AGCCTRL0, AGC_SAMPLES);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_MCSM0, MANUAL_CALIBRATION);
  if (error != ESP_OK)
    goto cleanup;
  while (atomic_load(&s_state) == FREQUENCY_WATERFALL_RUNNING) {
    if (atomic_load(&s_pause)) {
      error = cc1101_set_preset(CC1101_PRESET_IDLE, 0);
      if (error != ESP_OK)
        goto cleanup;
      portENTER_CRITICAL(&s_lock);
      s_snapshot.is_paused = true;
      portEXIT_CRITICAL(&s_lock);
      while (atomic_load(&s_pause) && atomic_load(&s_state) == FREQUENCY_WATERFALL_RUNNING)
        vTaskDelay(pdMS_TO_TICKS(PAUSE_POLL_MS));
      continue;
    }
    int64_t started = esp_timer_get_time();
    uint16_t bin;
    for (bin = 0; bin < FREQUENCY_WATERFALL_BINS; ++bin) {
      if (atomic_load(&s_state) != FREQUENCY_WATERFALL_RUNNING || atomic_load(&s_pause))
        break;
      error = cc1101_scan_rssi(frequency_waterfall_bin_hz(&line.config, bin), &line.dbm[bin]);
      if (error == ESP_OK && !isfinite(line.dbm[bin]))
        error = ESP_FAIL;
      if (error != ESP_OK)
        goto cleanup;
      portENTER_CRITICAL(&s_lock);
      s_snapshot.progress = bin + 1;
      s_snapshot.is_paused = false;
      portEXIT_CRITICAL(&s_lock);
    }
    if (bin != FREQUENCY_WATERFALL_BINS || atomic_load(&s_pause) ||
        atomic_load(&s_state) != FREQUENCY_WATERFALL_RUNNING)
      continue;
    line.duration_us = esp_timer_get_time() - started;
    line.sequence++;
    line.progress = FREQUENCY_WATERFALL_BINS;
    portENTER_CRITICAL(&s_lock);
    s_snapshot = line;
    portEXIT_CRITICAL(&s_lock);
    vTaskDelay(1);
  }
cleanup:
  if (error != ESP_OK)
    record_error(error);
  while (has_radio && (error = cc1101_set_preset(CC1101_PRESET_IDLE, 0)) != ESP_OK) {
    record_error(error);
    vTaskDelay(pdMS_TO_TICKS(CLEANUP_RETRY_MS));
  }
  resource_release(s_grant);
  atomic_store(&s_state, FREQUENCY_WATERFALL_IDLE);
  vTaskDelete(NULL);
}
