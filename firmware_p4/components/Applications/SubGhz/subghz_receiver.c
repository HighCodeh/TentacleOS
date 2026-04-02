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

#include "subghz_receiver.h"

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "cc1101.h"
#include "pin_def.h"
#include "subghz_protocol_registry.h"
#include "subghz_analyzer.h"
#include "subghz_storage.h"

static const char *TAG = "SUBGHZ_RX";

#define RMT_RX_GPIO            ((gpio_num_t)GPIO_SDA_PIN)
#define RMT_RESOLUTION_HZ      1000000
#define RX_BUFFER_SIZE          1024
#define DECODE_BUFFER_FACTOR    2
#define MIN_PULSE_NS            1000
#define SOFTWARE_FILTER_US      15
#define MAX_PULSE_NS            10000000
#define RMT_MEM_BLOCK_SYMBOLS   256
#define RMT_DMA_ALIGNMENT       64
#define RX_QUEUE_DEPTH          1
#define RX_QUEUE_TIMEOUT_MS     100
#define HOP_INTERVAL_MS         5000
#define RX_TASK_STACK_SIZE      8192
#define RX_TASK_PRIORITY        5
#define RX_TASK_CORE            1
#define FILENAME_BUF_SIZE       32
#define HEX_BUF_SIZE            65
#define MAX_HEX_BYTES           32
#define RAW_PREVIEW_COUNT       20
#define DEFAULT_FREQ            433920000

static const uint32_t HOP_FREQUENCIES[] = {
  433920000,
  868350000,
  315000000,
  300000000,
  390000000,
  418000000,
  915000000,
  302750000,
  303870000,
  304250000,
  310000000,
  318000000
};
#define HOP_FREQUENCIES_COUNT (sizeof(HOP_FREQUENCIES) / sizeof(HOP_FREQUENCIES[0]))

static TaskHandle_t s_rx_task_handle = NULL;
static volatile bool s_is_running = false;
static subghz_mode_t s_rx_mode = SUBGHZ_MODE_SCAN;
static cc1101_preset_t s_rx_preset = CC1101_PRESET_OOK_800KHZ;
static uint32_t s_rx_freq = DEFAULT_FREQ;
static bool s_hopping_active = false;
static int s_hop_idx = 0;
static uint32_t s_capture_count = 0;

static rmt_channel_handle_t s_rx_channel = NULL;
static QueueHandle_t s_rx_queue = NULL;

static void get_dynamic_filename(char *out_name, size_t out_size, const char *prefix) {
  s_capture_count++;
  snprintf(out_name, out_size, "%s_%03lu", prefix, (unsigned long)s_capture_count);
}

static bool subghz_rx_done_callback(rmt_channel_handle_t channel,
                                    const rmt_rx_done_event_data_t *edata,
                                    void *user_data) {
  BaseType_t high_task_wakeup = pdFALSE;
  QueueHandle_t queue = (QueueHandle_t)user_data;
  xQueueSendFromISR(queue, edata, &high_task_wakeup);
  return high_task_wakeup == pdTRUE;
}

static size_t decode_rmt_symbols(const rmt_symbol_word_t *symbols,
                                 size_t num_symbols,
                                 int32_t *decode_buffer,
                                 size_t decode_max) {
  size_t idx = 0;
  for (size_t i = 0; i < num_symbols; i++) {
    const rmt_symbol_word_t *sym = &symbols[i];
    if (sym->duration0 >= SOFTWARE_FILTER_US) {
      int32_t val = sym->level0 ? (int32_t)sym->duration0 : -(int32_t)sym->duration0;
      if (idx < decode_max) {
        decode_buffer[idx++] = val;
      }
    }
    if (sym->duration1 >= SOFTWARE_FILTER_US) {
      int32_t val = sym->level1 ? (int32_t)sym->duration1 : -(int32_t)sym->duration1;
      if (idx < decode_max) {
        decode_buffer[idx++] = val;
      }
    }
  }
  return idx;
}

static void handle_raw_mode(const int32_t *decode_buffer, size_t decode_idx) {
  char filename[FILENAME_BUF_SIZE];
  ESP_LOGD(TAG, "RAW: received %d pulses", (int)decode_idx);
  get_dynamic_filename(filename, sizeof(filename), "RAW");
  subghz_storage_save_raw(filename, decode_buffer, decode_idx, s_rx_freq);
}

static void log_recovered_bitstream(const subghz_analyzer_result_t *analysis) {
  if (analysis->bitstream_len == 0) {
    return;
  }

  char hex_buf[HEX_BUF_SIZE] = {0};
  size_t bytes_to_show = analysis->bitstream_len / 8;

  if (bytes_to_show > MAX_HEX_BYTES) {
    bytes_to_show = MAX_HEX_BYTES;
  }

  size_t offset = 0;

  for (size_t b = 0; b < bytes_to_show; b++) {
    int written = snprintf(&hex_buf[offset], sizeof(hex_buf) - offset,
                           "%02X", analysis->bitstream[b]);
    if (written > 0) {
      offset += (size_t)written;
    }
  }

  ESP_LOGI(TAG, "Recovered Bitstream (Hex): %s...", hex_buf);
}

static void handle_scan_mode(const int32_t *decode_buffer, size_t decode_idx) {
  char filename[FILENAME_BUF_SIZE];
  subghz_data_t decoded = {0};

  if (subghz_protocol_registry_decode_all(decode_buffer, decode_idx, &decoded)) {
    ESP_LOGI(TAG, "DECODED: Protocol: %s | Serial: 0x%lX | Btn: 0x%X | Bits: %d | Freq: %lu",
             decoded.protocol_name, decoded.serial, decoded.btn,
             decoded.bit_count, (unsigned long)s_rx_freq);

    subghz_analyzer_result_t analysis = {0};
    subghz_analyzer_process(decode_buffer, decode_idx, &analysis);
    get_dynamic_filename(filename, sizeof(filename), "DEC");
    subghz_storage_save_decoded(filename, &decoded, s_rx_freq, analysis.estimated_te);
    return;
  }

  subghz_analyzer_result_t analysis = {0};
  if (subghz_analyzer_process(decode_buffer, decode_idx, &analysis)) {
    ESP_LOGW(TAG, "Unknown Signal: TE ~%luus | Peaks: %s | Count: %d | Freq: %lu",
             (unsigned long)analysis.estimated_te, analysis.modulation_hint,
             (int)decode_idx, (unsigned long)s_rx_freq);

    get_dynamic_filename(filename, sizeof(filename), "UNK");
    subghz_storage_save_raw(filename, decode_buffer, decode_idx, s_rx_freq);
    log_recovered_bitstream(&analysis);
  }

  size_t preview = decode_idx > RAW_PREVIEW_COUNT ? RAW_PREVIEW_COUNT : decode_idx;
  ESP_LOGD(TAG, "RAW preview (%d of %d pulses):", (int)preview, (int)decode_idx);
  for (size_t k = 0; k < preview; k++) {
    ESP_LOGD(TAG, "  [%d] %ld", (int)k, (long)decode_buffer[k]);
  }
}

static void handle_frequency_hopping(TickType_t *last_hop_time, TickType_t hop_interval) {
  if (!s_hopping_active) {
    return;
  }

  if ((xTaskGetTickCount() - *last_hop_time) <= hop_interval) {
    return;
  }

  s_hop_idx = (s_hop_idx + 1) % (int)HOP_FREQUENCIES_COUNT;
  s_rx_freq = HOP_FREQUENCIES[s_hop_idx];

  ESP_LOGI(TAG, "Hopping to: %lu Hz", (unsigned long)s_rx_freq);

  cc1101_strobe(CC1101_SIDLE);
  cc1101_set_frequency(s_rx_freq);
  cc1101_strobe(CC1101_SRX);

  *last_hop_time = xTaskGetTickCount();
}

static void subghz_rx_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting RMT RX Task - Mode: %s, Preset: %d, Freq: %lu",
           s_rx_mode == SUBGHZ_MODE_RAW ? "RAW" : "SCAN",
           (int)s_rx_preset, (unsigned long)s_rx_freq);

  rmt_rx_channel_config_t rx_channel_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = RMT_RESOLUTION_HZ,
    .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
    .gpio_num = RMT_RX_GPIO,
    .flags.invert_in = false,
    .flags.with_dma = true,
  };

  ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &s_rx_channel));

  s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rmt_rx_done_event_data_t));
  rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = subghz_rx_done_callback,
  };
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_channel, &cbs, s_rx_queue));
  ESP_ERROR_CHECK(rmt_enable(s_rx_channel));

  cc1101_set_preset(s_rx_preset, s_rx_freq);

  s_is_running = true;
  ESP_LOGI(TAG, "Waiting for signals...");

  rmt_receive_config_t receive_config = {
    .signal_range_min_ns = MIN_PULSE_NS,
    .signal_range_max_ns = MAX_PULSE_NS,
  };

  rmt_rx_done_event_data_t rx_data;
  rmt_symbol_word_t *raw_symbols = NULL;
  int32_t *decode_buffer = NULL;

  size_t raw_symbols_size = sizeof(rmt_symbol_word_t) * RX_BUFFER_SIZE;
  raw_symbols = heap_caps_aligned_alloc(RMT_DMA_ALIGNMENT, raw_symbols_size,
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (raw_symbols == NULL) {
    ESP_LOGE(TAG, "Failed to allocate RMT RX buffer");
    goto cleanup;
  }

  decode_buffer = malloc(RX_BUFFER_SIZE * DECODE_BUFFER_FACTOR * sizeof(int32_t));
  if (decode_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate decode buffer");
    goto cleanup;
  }

  ESP_ERROR_CHECK(rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config));

  TickType_t last_hop_time = xTaskGetTickCount();
  const TickType_t hop_interval = pdMS_TO_TICKS(HOP_INTERVAL_MS);

  while (s_is_running) {
    handle_frequency_hopping(&last_hop_time, hop_interval);

    if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(RX_QUEUE_TIMEOUT_MS)) != pdPASS) {
      continue;
    }

    if (rx_data.num_symbols == 0) {
      if (s_is_running) {
        ESP_ERROR_CHECK(rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config));
      }
      continue;
    }

    size_t decode_idx = decode_rmt_symbols(rx_data.received_symbols,
                                           rx_data.num_symbols,
                                           decode_buffer,
                                           RX_BUFFER_SIZE * DECODE_BUFFER_FACTOR);

    if (decode_idx > 0) {
      if (s_rx_mode == SUBGHZ_MODE_RAW) {
        handle_raw_mode(decode_buffer, decode_idx);
      } else {
        handle_scan_mode(decode_buffer, decode_idx);
      }
    }

    if (s_is_running) {
      ESP_ERROR_CHECK(rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config));
    }
  }

cleanup:
  if (raw_symbols != NULL) {
    free(raw_symbols);
  }
  if (decode_buffer != NULL) {
    free(decode_buffer);
  }
  cc1101_strobe(CC1101_SIDLE);
  rmt_disable(s_rx_channel);
  rmt_del_channel(s_rx_channel);
  vQueueDelete(s_rx_queue);
  s_rx_channel = NULL;
  s_rx_queue = NULL;
  vTaskDelete(NULL);
}

esp_err_t subghz_receiver_start(subghz_mode_t mode, cc1101_preset_t preset, uint32_t freq) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }
  s_rx_mode = mode;
  s_rx_preset = preset;

  if (freq == 0) {
    s_hopping_active = true;
    s_hop_idx = 0;
    s_rx_freq = HOP_FREQUENCIES[0];
  } else {
    s_hopping_active = false;
    s_rx_freq = freq;
  }

  BaseType_t ret = xTaskCreatePinnedToCore(subghz_rx_task, "subghz_rx",
                                           RX_TASK_STACK_SIZE, NULL,
                                           RX_TASK_PRIORITY, &s_rx_task_handle,
                                           RX_TASK_CORE);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create RX task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void subghz_receiver_stop(void) {
  s_is_running = false;
}

bool subghz_receiver_is_running(void) {
  return s_is_running;
}
