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

#include "hb_nfc_v_rx.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EDGE_CAPACITY      256U
#define CAPTURE_TIMEOUT_US 250000U
#define WAIT_TIMEOUT_US    10000U
#define POLL_US            3U
#define ISR_GUARD_US       4U

static const char *TAG = "NFC_V_RX";

typedef struct {
  uint64_t time_us;
  bool level;
} hb_nfc_v_rx_edge_t;

static DRAM_ATTR hb_nfc_v_rx_edge_t s_edges[EDGE_CAPACITY];
static DRAM_ATTR portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR size_t s_read;
static DRAM_ATTR size_t s_write;
static DRAM_ATTR bool s_overflow;
static DRAM_ATTR gpio_num_t s_pin = GPIO_NUM_NC;
static bool s_is_ready;

static void IRAM_ATTR capture_edge(void *context) {
  (void)context;
  uint64_t time_us = (uint64_t)esp_timer_get_time();
  bool level = gpio_ll_get_level(&GPIO, s_pin) != 0;
  portENTER_CRITICAL_ISR(&s_lock);
  size_t next = (s_write + 1) % EDGE_CAPACITY;
  if (next == s_read) {
    s_overflow = true;
  } else {
    s_edges[s_write] = (hb_nfc_v_rx_edge_t){time_us, level};
    s_write = next;
  }
  portEXIT_CRITICAL_ISR(&s_lock);
}

esp_err_t hb_nfc_v_rx_init(gpio_num_t pin) {
  if (!GPIO_IS_VALID_GPIO(pin))
    return ESP_ERR_INVALID_ARG;
  if (s_is_ready)
    return pin == s_pin ? ESP_OK : ESP_ERR_INVALID_STATE;
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    return err;
  s_pin = pin;
  err = gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
  if (err != ESP_OK)
    return err;
  err = gpio_isr_handler_add(pin, capture_edge, NULL);
  if (err != ESP_OK)
    return err;
  s_is_ready = true;
  err = gpio_intr_disable(pin);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Cannot disable receive IRQ: %s", esp_err_to_name(err));
  return err;
}

esp_err_t hb_nfc_v_rx_deinit(void) {
  if (!s_is_ready)
    return ESP_OK;
  esp_err_t err = gpio_intr_disable(s_pin);
  if (err != ESP_OK)
    return err;
  err = gpio_isr_handler_remove(s_pin);
  if (err != ESP_OK)
    return err;
  s_is_ready = false;
  s_pin = GPIO_NUM_NC;
  return ESP_OK;
}

esp_err_t hb_nfc_v_rx_capture(
    uint8_t *buffer, size_t capacity, size_t *out_length, bool *out_slot, int64_t *out_eof_us) {
  if (!out_length || !out_slot || !out_eof_us)
    return ESP_ERR_INVALID_ARG;
  *out_length = 0;
  *out_slot = false;
  *out_eof_us = 0;
  if (!s_is_ready)
    return ESP_ERR_INVALID_STATE;
  hb_nfc_v_codec_edges_t decoder;
  esp_err_t err = hb_nfc_v_codec_edges_init(&decoder, buffer, capacity);
  if (err != ESP_OK)
    return err;
  portENTER_CRITICAL(&s_lock);
  s_read = s_write = 0;
  s_overflow = false;
  portEXIT_CRITICAL(&s_lock);
  err = gpio_intr_enable(s_pin);
  if (err != ESP_OK)
    return err;
  uint64_t start = (uint64_t)esp_timer_get_time();
  err = ESP_ERR_TIMEOUT;
  for (;;) {
    hb_nfc_v_rx_edge_t edge = {0};
    portENTER_CRITICAL(&s_lock);
    bool has_edge = s_read != s_write;
    bool overflow = s_overflow;
    if (has_edge) {
      edge = s_edges[s_read];
      s_read = (s_read + 1) % EDGE_CAPACITY;
    }
    portEXIT_CRITICAL(&s_lock);
    if (overflow) {
      err = ESP_ERR_INVALID_SIZE;
      break;
    }
    uint64_t now = (uint64_t)esp_timer_get_time();
    hb_nfc_v_codec_rx_result_t result;
    if (has_edge) {
      result = hb_nfc_v_codec_edges_feed(&decoder, edge.time_us, edge.level);
    } else {
      uint64_t safe_time = now > ISR_GUARD_US ? now - ISR_GUARD_US : 0;
      result = safe_time >= decoder.last_us ? hb_nfc_v_codec_edges_advance(&decoder, safe_time)
                                            : decoder.decoder.result;
    }
    if (result == HB_NFC_V_CODEC_RX_FRAME || result == HB_NFC_V_CODEC_RX_SLOT) {
      *out_length = decoder.decoder.length;
      *out_slot = result == HB_NFC_V_CODEC_RX_SLOT;
      *out_eof_us = (int64_t)decoder.fall_us;
      err = ESP_OK;
      break;
    }
    if (result != HB_NFC_V_CODEC_RX_WAIT) {
      err = ESP_ERR_INVALID_RESPONSE;
      break;
    }
    if (now - start >= (decoder.is_started ? CAPTURE_TIMEOUT_US : WAIT_TIMEOUT_US))
      break;
    if (!has_edge)
      esp_rom_delay_us(POLL_US);
  }
  esp_err_t stop_err = gpio_intr_disable(s_pin);
  if (stop_err != ESP_OK)
    err = stop_err;
  if (err != ESP_OK) {
    *out_length = 0;
    *out_slot = false;
    *out_eof_us = 0;
  }
  return err;
}
