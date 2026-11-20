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

#include "lf_rfid.h"
#include "lf_rfid_decoder.h"

#include <string.h>

#include "driver/mcpwm_cap.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_def.h"
#include "rf_frontend.h"

#define CAPTURE_HZ       1000000U
#define RING_COUNT       512U
#define BATCH_COUNT      32U
#define READER_SETTLE_MS 450U
#define READER_WAIT_MS   5U
#define READER_MIN_MS    40U
#define READER_MAX_MS    1000U
#define BYTE_BITS        8U
#define BYTE_MASK        0xffU
static const char *TAG = "LF_READER";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
// Capture callbacks need internal RAM even when the display accesses PSRAM.
static DRAM_ATTR lf_rfid_edge_t s_ring[RING_COUNT];
static uint32_t s_write;
static uint32_t s_read;
static uint32_t s_previous;
static bool s_has_previous;
static lf_rfid_read_stats_t s_stats;
static mcpwm_cap_timer_handle_t s_timer;
static mcpwm_cap_channel_handle_t s_channel;
static bool s_timer_enabled;
static bool s_timer_started;
static bool s_channel_enabled;

static bool IRAM_ATTR captured(mcpwm_cap_channel_handle_t channel,
                               const mcpwm_capture_event_data_t *event,
                               void *ctx) {
  (void)channel;
  (void)ctx;
  portENTER_CRITICAL_ISR(&s_lock);
  s_stats.edges++;
  if (s_has_previous) {
    if (s_write - s_read >= RING_COUNT) {
      s_stats.dropped++;
    } else {
      s_ring[s_write % RING_COUNT] = (lf_rfid_edge_t){
          .delta_us = event->cap_value - s_previous,
          .level = event->cap_edge == MCPWM_CAP_EDGE_NEG,
      };
      s_write++;
    }
  }
  s_previous = event->cap_value;
  s_has_previous = true;
  portEXIT_CRITICAL_ISR(&s_lock);
  return false;
}

static esp_err_t cleanup(void) {
  esp_err_t error;
  if (s_channel_enabled) {
    error = mcpwm_capture_channel_disable(s_channel);
    if (error != ESP_OK)
      return error;
    s_channel_enabled = false;
  }
  if (s_channel != NULL) {
    error = mcpwm_del_capture_channel(s_channel);
    if (error != ESP_OK)
      return error;
    s_channel = NULL;
  }
  if (s_timer_started) {
    error = mcpwm_capture_timer_stop(s_timer);
    if (error != ESP_OK)
      return error;
    s_timer_started = false;
  }
  if (s_timer_enabled) {
    error = mcpwm_capture_timer_disable(s_timer);
    if (error != ESP_OK)
      return error;
    s_timer_enabled = false;
  }
  if (s_timer != NULL) {
    error = mcpwm_del_capture_timer(s_timer);
    if (error != ESP_OK)
      return error;
    s_timer = NULL;
  }
  return ESP_OK;
}

void lf_rfid_read_stats(lf_rfid_read_stats_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_stats;
  portEXIT_CRITICAL(&s_lock);
}

esp_err_t lf_rfid_reader_quiesce(void) {
  portENTER_CRITICAL(&s_lock);
  bool busy = s_stats.running;
  if (!busy)
    s_stats.running = true;
  portEXIT_CRITICAL(&s_lock);
  if (busy)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = cleanup();
  portENTER_CRITICAL(&s_lock);
  s_stats.running = false;
  portEXIT_CRITICAL(&s_lock);
  return error;
}

static esp_err_t start(void) {
  esp_err_t error = cleanup();
  if (error != ESP_OK)
    return error;
  mcpwm_capture_timer_config_t config = {
      .group_id = 0,
      .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
      .resolution_hz = CAPTURE_HZ,
  };
  error = mcpwm_new_capture_timer(&config, &s_timer);
  if (error != ESP_OK)
    return error;
  uint32_t resolution = 0;
  error = mcpwm_capture_timer_get_resolution(s_timer, &resolution);
  if (error != ESP_OK)
    return error;
  if (resolution != CAPTURE_HZ)
    return ESP_ERR_NOT_SUPPORTED;
  mcpwm_capture_channel_config_t channel = {
      .gpio_num = GPIO_RFID_LF_DATA_PIN,
      .prescale = 1,
      .flags.pos_edge = true,
      .flags.neg_edge = true,
  };
  error = mcpwm_new_capture_channel(s_timer, &channel, &s_channel);
  if (error != ESP_OK)
    return error;
  mcpwm_capture_event_callbacks_t callbacks = {.on_cap = captured};
  error = mcpwm_capture_channel_register_event_callbacks(s_channel, &callbacks, NULL);
  if (error != ESP_OK)
    return error;
  error = mcpwm_capture_timer_enable(s_timer);
  if (error != ESP_OK)
    return error;
  s_timer_enabled = true;
  error = mcpwm_capture_timer_start(s_timer);
  if (error != ESP_OK)
    return error;
  s_timer_started = true;
  portENTER_CRITICAL(&s_lock);
  s_write = s_read = 0;
  s_has_previous = false;
  s_stats = (lf_rfid_read_stats_t){.running = true};
  portEXIT_CRITICAL(&s_lock);
  error = mcpwm_capture_channel_enable(s_channel);
  if (error == ESP_OK)
    s_channel_enabled = true;
  return error;
}

esp_err_t lf_rfid_read_em4100(uint32_t timeout_ms, lf_rfid_em4100_t *out) {
  if (out == NULL || timeout_ms < READER_MIN_MS || timeout_ms > READER_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  if (!lf_rfid_field_is_on() || !rf_frontend_can_access(RF_FRONTEND_LF_125KHZ))
    return ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&s_lock);
  bool busy = s_stats.running;
  if (!busy)
    s_stats = (lf_rfid_read_stats_t){.running = true};
  portEXIT_CRITICAL(&s_lock);
  if (busy)
    return ESP_ERR_INVALID_STATE;
  uint32_t age = lf_rfid_field_age_ms();
  if (age < READER_SETTLE_MS)
    vTaskDelay(pdMS_TO_TICKS(READER_SETTLE_MS - age));
  esp_err_t error = start();
  lf_rfid_decoder_t decoder;
  lf_rfid_decoder_reset(&decoder);
  if (error == ESP_OK) {
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    error = ESP_ERR_NOT_FOUND;
    while (esp_timer_get_time() < deadline) {
      lf_rfid_edge_t edges[BATCH_COUNT];
      size_t count = 0;
      portENTER_CRITICAL(&s_lock);
      while (count < BATCH_COUNT && s_read != s_write)
        edges[count++] = s_ring[s_read++ % RING_COUNT];
      uint32_t dropped = s_stats.dropped;
      portEXIT_CRITICAL(&s_lock);
      if (dropped != 0) {
        error = ESP_ERR_INVALID_SIZE;
        break;
      }
      for (size_t i = 0; i < count; i++) {
        lf_rfid_decoder_feed(&decoder, edges[i].delta_us, edges[i].level);
        if (decoder.is_valid)
          break;
      }
      portENTER_CRITICAL(&s_lock);
      s_stats.invalid_pulses = decoder.invalid_pulses;
      portEXIT_CRITICAL(&s_lock);
      if (decoder.is_valid) {
        error = ESP_OK;
        break;
      }
      if (count == 0)
        vTaskDelay(pdMS_TO_TICKS(READER_WAIT_MS));
    }
  }
  esp_err_t stopped = cleanup();
  portENTER_CRITICAL(&s_lock);
  s_stats.running = false;
  if (s_stats.dropped != 0 && error == ESP_OK)
    error = ESP_ERR_INVALID_SIZE;
  portEXIT_CRITICAL(&s_lock);
  if (stopped != ESP_OK) {
    ESP_LOGE(TAG, "Capture OFF failed: %s", esp_err_to_name(stopped));
    return stopped;
  }
  if (error == ESP_OK) {
    out->id = decoder.id;
    out->inverted = decoder.inverted;
    for (uint32_t i = 0; i < LF_RFID_EM4100_BYTES; i++)
      out->bytes[i] = (decoder.id >> ((LF_RFID_EM4100_BYTES - 1U - i) * BYTE_BITS)) & BYTE_MASK;
  }
  return error;
}
