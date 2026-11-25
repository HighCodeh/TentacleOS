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

#include "frequency_analyzer.h"

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

#define WORKER_STACK         4096
#define IDLE_POLL_MS         20
#define CLEANUP_RETRY_MS     100
#define WIDE_BANDWIDTH_KHZ   650.0f
#define FINE_BANDWIDTH_KHZ   58.035714f
#define FINE_RADIUS_HZ       300000
#define RSSI_FLOOR_DBM       (-130.0f)
#define RSSI_STATUS_READ     0x40U
#define PEAK_TIE_DB          1.0f
#define MIN_PEAK_CONTRAST_DB 6.0f
#define FINE_SAMPLES         (2 * FINE_RADIUS_HZ / FREQUENCY_ANALYZER_FINE_STEP_HZ + 1)
#define HISTORY_MATCH_HZ     100000U
#define AGC_TARGET           0x07U
#define AGC_CARRIER_SENSE    0x08U
#define AGC_SAMPLES          0x30U
#define ANALYZER_SYMBOL_RATE 0x7FU

typedef enum { WORKER_IDLE, WORKER_STARTING, WORKER_RUNNING, WORKER_STOPPING } worker_state_t;
static const char *TAG = "FREQUENCY_ANALYZER";
static atomic_uint s_state, s_generation;
static atomic_bool s_paused;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
// Small bounded control snapshot, copied under a critical section; no DMA buffers.
static frequency_analyzer_snapshot_t s_snapshot;
static frequency_analyzer_mode_t s_mode;
static int16_t s_threshold;
static res_handle_t s_grant;
static void worker(void *argument);

static bool valid_config(frequency_analyzer_mode_t mode, int16_t threshold) {
  return frequency_analyzer_plan_count(mode) != 0 &&
         threshold >= FREQUENCY_ANALYZER_THRESHOLD_MIN &&
         threshold <= FREQUENCY_ANALYZER_THRESHOLD_MAX;
}

esp_err_t frequency_analyzer_configure(frequency_analyzer_mode_t mode, int16_t threshold_dbm) {
  if (!valid_config(mode, threshold_dbm))
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  s_mode = mode;
  s_threshold = threshold_dbm;
  atomic_fetch_add(&s_generation, 1);
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

esp_err_t frequency_analyzer_start(frequency_analyzer_mode_t mode, int16_t threshold_dbm) {
  if (!valid_config(mode, threshold_dbm))
    return ESP_ERR_INVALID_ARG;
  unsigned expected = WORKER_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, WORKER_STARTING))
    return ESP_ERR_INVALID_STATE;
  res_request_t request = {
      .id = RES_SUBGHZ, .lane = RES_LANE_MAIN, .owner_kind = RES_OWNER_UI, .allow_preempt = false};
  esp_err_t error = resource_acquire(&request, &s_grant);
  if (error != ESP_OK) {
    atomic_store(&s_state, WORKER_IDLE);
    return error;
  }
  portENTER_CRITICAL(&s_lock);
  s_mode = mode;
  s_threshold = threshold_dbm;
  s_snapshot = (frequency_analyzer_snapshot_t){.mode = mode,
                                               .threshold_dbm = threshold_dbm,
                                               .phase = FREQUENCY_ANALYZER_SEARCHING,
                                               .rssi_dbm = RSSI_FLOOR_DBM,
                                               .live_dbm = RSSI_FLOOR_DBM};
  portEXIT_CRITICAL(&s_lock);
  atomic_store(&s_paused, false);
  atomic_fetch_add(&s_generation, 1);
  expected = WORKER_STARTING;
  atomic_compare_exchange_strong(&s_state, &expected, WORKER_RUNNING);
  if (xTaskCreatePinnedToCore(worker,
                              "freq_analyzer",
                              WORKER_STACK,
                              NULL,
                              SYS_PRIO_BACKGROUND_LO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    resource_release(s_grant);
    s_grant = RES_HANDLE_NONE;
    atomic_store(&s_state, WORKER_IDLE);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
void frequency_analyzer_stop(void) {
  unsigned state = atomic_load(&s_state);
  while (state == WORKER_RUNNING || state == WORKER_STARTING)
    if (atomic_compare_exchange_weak(&s_state, &state, WORKER_STOPPING))
      break;
}
bool frequency_analyzer_is_running(void) {
  return atomic_load(&s_state) != WORKER_IDLE;
}
void frequency_analyzer_pause(bool is_paused) {
  atomic_store(&s_paused, is_paused);
}
void frequency_analyzer_get_snapshot(frequency_analyzer_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out_snapshot = s_snapshot;
  portEXIT_CRITICAL(&s_lock);
}
static bool is_cancelled(unsigned generation) {
  return atomic_load(&s_state) != WORKER_RUNNING || atomic_load(&s_paused) ||
         atomic_load(&s_generation) != generation;
}
static esp_err_t set_bandwidth(float bandwidth) {
  esp_err_t error = cc1101_strobe(CC1101_SIDLE);
  if (error == ESP_OK)
    error = cc1101_set_rx_bandwidth(bandwidth);
  return error;
}
static esp_err_t sample(uint32_t frequency, float *out_dbm) {
  esp_err_t error = cc1101_scan_rssi(frequency, out_dbm);
  if (error != ESP_OK)
    return error;
  if (!isfinite(*out_dbm))
    return ESP_FAIL;
  portENTER_CRITICAL(&s_lock);
  s_snapshot.samples++;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}
static uint32_t distance(uint32_t a, uint32_t b) {
  return a > b ? a - b : b - a;
}
static void record_hit(uint32_t frequency, float rssi) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot.frequency_hz = frequency;
  s_snapshot.rssi_dbm = rssi;
  s_snapshot.phase = FREQUENCY_ANALYZER_FOUND;
  s_snapshot.detections++;
  unsigned slot = FREQUENCY_ANALYZER_HISTORY - 1;
  for (unsigned i = 0; i < FREQUENCY_ANALYZER_HISTORY; i++) {
    if (s_snapshot.history[i].frequency_hz &&
        distance(s_snapshot.history[i].frequency_hz, frequency) <= HISTORY_MATCH_HZ) {
      slot = i;
      break;
    }
  }
  for (unsigned i = slot; i > 0; i--)
    s_snapshot.history[i] = s_snapshot.history[i - 1];
  s_snapshot.history[0] = (frequency_analyzer_hit_t){frequency, rssi};
  portEXIT_CRITICAL(&s_lock);
}
static esp_err_t refine(uint32_t coarse, unsigned generation, int16_t threshold) {
  int64_t started_us = esp_timer_get_time();
  esp_err_t error = set_bandwidth(FINE_BANDWIDTH_KHZ);
  if (error != ESP_OK)
    return error;
  uint32_t peak_frequency = 0;
  float peak = RSSI_FLOOR_DBM, floor = 0;
  float samples[FINE_SAMPLES];
  uint32_t frequencies[FINE_SAMPLES];
  unsigned count = 0;
  portENTER_CRITICAL(&s_lock);
  s_snapshot.phase = FREQUENCY_ANALYZER_REFINING;
  portEXIT_CRITICAL(&s_lock);
  for (int32_t delta = -FINE_RADIUS_HZ; delta <= FINE_RADIUS_HZ;
       delta += FREQUENCY_ANALYZER_FINE_STEP_HZ) {
    if (is_cancelled(generation))
      return ESP_OK;
    uint32_t frequency = (uint32_t)((int64_t)coarse + delta);
    if (cc1101_rf_path_for_frequency(frequency) == CC1101_RF_PATH_NONE)
      continue;
    float rssi;
    error = sample(frequency, &rssi);
    if (error != ESP_OK)
      return error;
    samples[count] = rssi;
    frequencies[count++] = frequency;
    if (rssi > peak)
      peak = rssi;
    if (rssi < floor)
      floor = rssi;
  }
  portENTER_CRITICAL(&s_lock);
  s_snapshot.refine_us = (uint32_t)(esp_timer_get_time() - started_us);
  portEXIT_CRITICAL(&s_lock);
  if (is_cancelled(generation) || peak < threshold || peak - floor < MIN_PEAK_CONTRAST_DB)
    return ESP_OK;
  for (unsigned i = 0; i < count; i++) {
    if (samples[i] >= peak - PEAK_TIE_DB &&
        (!peak_frequency || distance(frequencies[i], coarse) < distance(peak_frequency, coarse)))
      peak_frequency = frequencies[i];
  }
  if (!peak_frequency)
    return ESP_OK;
  // Confirm the peak again; disappearing bursts must not become a confirmed result.
  float confirm;
  error = sample(peak_frequency, &confirm);
  if (error != ESP_OK || is_cancelled(generation))
    return error;
  if (confirm >= threshold) {
    record_hit(peak_frequency, confirm);
  }
  return ESP_OK;
}
static void record_error(esp_err_t error) {
  portENTER_CRITICAL(&s_lock);
  bool is_first = s_snapshot.error == ESP_OK;
  if (is_first)
    s_snapshot.error = error;
  s_snapshot.phase = FREQUENCY_ANALYZER_ERROR;
  portEXIT_CRITICAL(&s_lock);
  if (is_first) {
    ESP_LOGE(TAG, "Radio stopped: %s", esp_err_to_name(error));
    led_signal_error();
  }
}
static void worker(void *argument) {
  (void)argument;
  esp_err_t error = ESP_OK;
  bool has_radio = false;
  if (atomic_load(&s_state) != WORKER_RUNNING)
    goto cleanup;
  has_radio = true;
  error = cc1101_init();
  if (error != ESP_OK)
    goto cleanup;
  portENTER_CRITICAL(&s_lock);
  frequency_analyzer_mode_t initial_mode = s_mode;
  portEXIT_CRITICAL(&s_lock);
  error = cc1101_set_preset(CC1101_PRESET_OOK_650KHZ,
                            frequency_analyzer_plan_frequency(initial_mode, 0));
  if (error != ESP_OK)
    goto cleanup;
  error = cc1101_strobe(CC1101_SIDLE);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_MDMCFG3, ANALYZER_SYMBOL_RATE);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_AGCCTRL2, AGC_TARGET);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_AGCCTRL1, AGC_CARRIER_SENSE);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_AGCCTRL0, AGC_SAMPLES);
  // Every scan point calibrates explicitly; avoid a second calibration on SRX.
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_MCSM0, 0x08U);
  if (error != ESP_OK)
    goto cleanup;
  while (atomic_load(&s_state) == WORKER_RUNNING) {
    if (atomic_load(&s_paused)) {
      error = cc1101_set_preset(CC1101_PRESET_IDLE, 0);
      if (error != ESP_OK)
        break;
      portENTER_CRITICAL(&s_lock);
      s_snapshot.phase = FREQUENCY_ANALYZER_PAUSED;
      portEXIT_CRITICAL(&s_lock);
      while (atomic_load(&s_paused) && atomic_load(&s_state) == WORKER_RUNNING)
        vTaskDelay(pdMS_TO_TICKS(IDLE_POLL_MS));
      continue;
    }
    portENTER_CRITICAL(&s_lock);
    frequency_analyzer_mode_t mode = s_mode;
    int16_t threshold = s_threshold;
    unsigned generation = atomic_load(&s_generation);
    if (s_snapshot.mode != mode) {
      s_snapshot.frequency_hz = 0;
      s_snapshot.trace_count = 0;
    }
    s_snapshot.mode = mode;
    s_snapshot.threshold_dbm = threshold;
    s_snapshot.phase = FREQUENCY_ANALYZER_SEARCHING;
    s_snapshot.total = frequency_analyzer_plan_count(mode);
    s_snapshot.progress = 0;
    portEXIT_CRITICAL(&s_lock);
    int64_t sweep_started_us = esp_timer_get_time();
    error = set_bandwidth(WIDE_BANDWIDTH_KHZ);
    if (error != ESP_OK)
      break;
    uint32_t best_frequency = 0;
    float best_rssi = RSSI_FLOOR_DBM;
    uint16_t total = frequency_analyzer_plan_count(mode);
    for (uint16_t i = 0; i < total && !is_cancelled(generation); i++) {
      uint32_t frequency = frequency_analyzer_plan_frequency(mode, i);
      float rssi;
      error = sample(frequency, &rssi);
      if (error != ESP_OK)
        goto cleanup;
      portENTER_CRITICAL(&s_lock);
      s_snapshot.scan_frequency_hz = frequency;
      s_snapshot.live_dbm = rssi;
      s_snapshot.progress = i + 1;
      if (s_snapshot.trace_count < FREQUENCY_ANALYZER_TRACE)
        s_snapshot.trace[s_snapshot.trace_count++] = rssi;
      else {
        memmove(s_snapshot.trace,
                s_snapshot.trace + 1,
                (FREQUENCY_ANALYZER_TRACE - 1) * sizeof(s_snapshot.trace[0]));
        s_snapshot.trace[FREQUENCY_ANALYZER_TRACE - 1] = rssi;
      }
      portEXIT_CRITICAL(&s_lock);
      if (rssi > best_rssi) {
        best_rssi = rssi;
        best_frequency = frequency;
      }
      if (mode != FREQUENCY_ANALYZER_COMMON && rssi >= threshold) {
        error = refine(frequency, generation, threshold);
        if (error != ESP_OK)
          goto cleanup;
        error = set_bandwidth(WIDE_BANDWIDTH_KHZ);
        if (error != ESP_OK)
          goto cleanup;
        portENTER_CRITICAL(&s_lock);
        s_snapshot.phase = FREQUENCY_ANALYZER_SEARCHING;
        portEXIT_CRITICAL(&s_lock);
      }
    }
    if (!is_cancelled(generation)) {
      if (mode == FREQUENCY_ANALYZER_COMMON && best_frequency && best_rssi >= threshold) {
        error = refine(best_frequency, generation, threshold);
        if (error != ESP_OK)
          break;
      }
      portENTER_CRITICAL(&s_lock);
      s_snapshot.sweeps++;
      s_snapshot.sweep_us = (uint32_t)(esp_timer_get_time() - sweep_started_us);
      portEXIT_CRITICAL(&s_lock);
    }
  }
cleanup:
  atomic_store(&s_state, WORKER_STOPPING);
  if (error != ESP_OK)
    record_error(error);
  while (has_radio && (error = cc1101_set_preset(CC1101_PRESET_IDLE, 0)) != ESP_OK) {
    record_error(error);
    vTaskDelay(pdMS_TO_TICKS(CLEANUP_RETRY_MS));
  }
  resource_release(s_grant);
  s_grant = RES_HANDLE_NONE;
  portENTER_CRITICAL(&s_lock);
  if (s_snapshot.error == ESP_OK)
    s_snapshot.phase = FREQUENCY_ANALYZER_STOPPED;
  portEXIT_CRITICAL(&s_lock);
  atomic_store(&s_state, WORKER_IDLE);
  vTaskDelete(NULL);
}
