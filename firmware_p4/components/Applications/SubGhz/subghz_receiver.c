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

#include "subghz_receiver.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "cc1101.h"
#include "pin_def.h"
#include "led_control.h"
#include "subghz_protocol_registry.h"
#include "subghz_analyzer.h"
#include "subghz_storage.h"
#include "subghz_transmitter.h"

static const char *TAG = "SUBGHZ_RX";

#define RMT_RESOLUTION_HZ     1000000
#define RX_BUFFER_SIZE        1024
#define DECODE_BUFFER_FACTOR  2
#define MIN_PULSE_NS          1000
#define SOFTWARE_FILTER_US    15
#define MAX_PULSE_NS          10000000
#define RMT_MEM_BLOCK_SYMBOLS 256
#define RMT_DMA_ALIGNMENT     64
#define RX_QUEUE_DEPTH        1
#define RX_QUEUE_TIMEOUT_MS   100
#define HOP_INTERVAL_MS       5000
#define RX_TASK_STACK_SIZE    8192
#define RX_TASK_PRIORITY      SYS_PRIO_SERVICE_HI
#define RX_TASK_CORE          SYS_CORE_RADIO
#define FILENAME_BUF_SIZE     32
#define HEX_BUF_SIZE          65
#define MAX_HEX_BYTES         32
#define RAW_PREVIEW_COUNT     20
#define DEFAULT_FREQ          433920000

static const uint32_t HOP_FREQUENCIES[] = {433920000,
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
                                           318000000};
#define HOP_FREQUENCIES_COUNT (sizeof(HOP_FREQUENCIES) / sizeof(HOP_FREQUENCIES[0]))

static TaskHandle_t s_rx_task_handle = NULL;
static volatile bool s_is_running = false;
static subghz_mode_t s_rx_mode = SUBGHZ_MODE_SCAN;
static cc1101_preset_t s_rx_preset = CC1101_PRESET_OOK_800KHZ;
static uint32_t s_rx_freq = DEFAULT_FREQ;
static bool s_is_hopping_active = false;
static int s_hop_idx = 0;
static uint32_t s_capture_count = 0;

static rmt_channel_handle_t s_rx_channel = NULL;
static QueueHandle_t s_rx_queue = NULL;

#define RESULT_LOCK_TIMEOUT_MS 20
static SemaphoreHandle_t s_result_mutex = NULL;
static subghz_rx_result_t s_latest;
static uint32_t s_result_seq = 0;

static void publish_rx_result(bool decoded,
                              const subghz_data_t *data,
                              const subghz_analyzer_result_t *analysis,
                              const char *save_name) {
  if (s_result_mutex == NULL)
    return;
  if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) != pdTRUE)
    return;

  s_result_seq++;
  s_latest.seq = s_result_seq;
  s_latest.decoded = decoded;
  s_latest.freq = s_rx_freq;
  if (save_name != NULL)
    strlcpy(s_latest.save_name, save_name, sizeof(s_latest.save_name));
  else
    s_latest.save_name[0] = '\0';

  if (data != NULL) {
    s_latest.data = *data;
    if (data->protocol_name != NULL)
      strlcpy(s_latest.name_buf, data->protocol_name, sizeof(s_latest.name_buf));
    else
      s_latest.name_buf[0] = '\0';
  } else {
    memset(&s_latest.data, 0, sizeof(s_latest.data));
    s_latest.name_buf[0] = '\0';
  }
  s_latest.data.protocol_name = s_latest.name_buf;

  if (analysis != NULL)
    s_latest.analysis = *analysis;
  else
    memset(&s_latest.analysis, 0, sizeof(s_latest.analysis));

  xSemaphoreGive(s_result_mutex);
}

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
  publish_rx_result(false, NULL, NULL, filename);
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
    int written =
        snprintf(&hex_buf[offset], sizeof(hex_buf) - offset, "%02X", analysis->bitstream[b]);
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
    ESP_LOGI(TAG,
             "DECODED: Protocol: %s | Serial: 0x%lX | Btn: 0x%X | Bits: %d | Freq: %lu",
             decoded.protocol_name,
             decoded.serial,
             decoded.btn,
             decoded.bit_count,
             (unsigned long)s_rx_freq);

    subghz_analyzer_result_t analysis = {0};
    subghz_analyzer_process(decode_buffer, decode_idx, &analysis);
    get_dynamic_filename(filename, sizeof(filename), "DEC");
    subghz_storage_save_decoded(filename, &decoded, s_rx_freq, analysis.estimated_te);
    publish_rx_result(true, &decoded, &analysis, filename);
    led_signal_info(); // decoded a known protocol
    return;
  }

  subghz_analyzer_result_t analysis = {0};
  if (subghz_analyzer_process(decode_buffer, decode_idx, &analysis)) {
    ESP_LOGW(TAG,
             "Unknown Signal: TE ~%luus | Peaks: %s | Count: %d | Freq: %lu",
             (unsigned long)analysis.estimated_te,
             analysis.modulation_hint,
             (int)decode_idx,
             (unsigned long)s_rx_freq);

    get_dynamic_filename(filename, sizeof(filename), "UNK");
    subghz_storage_save_raw(filename, decode_buffer, decode_idx, s_rx_freq);
    publish_rx_result(false, NULL, &analysis, filename);
    log_recovered_bitstream(&analysis);
    led_signal_warning(); // captured RF but no known protocol matched
  }

  size_t preview = decode_idx > RAW_PREVIEW_COUNT ? RAW_PREVIEW_COUNT : decode_idx;
  ESP_LOGD(TAG, "RAW preview (%d of %d pulses):", (int)preview, (int)decode_idx);
  for (size_t k = 0; k < preview; k++) {
    ESP_LOGD(TAG, "  [%d] %ld", (int)k, (long)decode_buffer[k]);
  }
}

static void handle_frequency_hopping(TickType_t *last_hop_time, TickType_t hop_interval) {
  if (s_is_hopping_active == false) {
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
  ESP_LOGI(TAG,
           "Starting RMT RX Task - Mode: %s, Preset: %d, Freq: %lu",
           s_rx_mode == SUBGHZ_MODE_RAW ? "RAW" : "SCAN",
           (int)s_rx_preset,
           (unsigned long)s_rx_freq);

  rmt_symbol_word_t *raw_symbols = NULL;
  int32_t *decode_buffer = NULL;
  esp_err_t err = ESP_OK;

  rmt_rx_channel_config_t rx_channel_cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = RMT_RESOLUTION_HZ,
      .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
      .gpio_num = GPIO_CC1101_GDO0_PIN,
      .flags.invert_in = false,
      .flags.with_dma = true,
  };

  err = rmt_new_rx_channel(&rx_channel_cfg, &s_rx_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s", esp_err_to_name(err));
    goto cleanup;
  }

  s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rmt_rx_done_event_data_t));
  rmt_rx_event_callbacks_t cbs = {
      .on_recv_done = subghz_rx_done_callback,
  };
  err = rmt_rx_register_event_callbacks(s_rx_channel, &cbs, s_rx_queue);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_rx_register_event_callbacks failed: %s", esp_err_to_name(err));
    goto cleanup;
  }
  err = rmt_enable(s_rx_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
    goto cleanup;
  }

  cc1101_set_preset(s_rx_preset, s_rx_freq);

  s_is_running = true;
  ESP_LOGI(TAG, "Waiting for signals...");

  rmt_receive_config_t receive_config = {
      .signal_range_min_ns = MIN_PULSE_NS,
      .signal_range_max_ns = MAX_PULSE_NS,
  };

  rmt_rx_done_event_data_t rx_data;

  size_t raw_symbols_size = sizeof(rmt_symbol_word_t) * RX_BUFFER_SIZE;
  raw_symbols = heap_caps_aligned_alloc(
      RMT_DMA_ALIGNMENT, raw_symbols_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (raw_symbols == NULL) {
    ESP_LOGE(TAG, "Failed to allocate RMT RX buffer");
    goto cleanup;
  }

  decode_buffer = malloc(RX_BUFFER_SIZE * DECODE_BUFFER_FACTOR * sizeof(int32_t));
  if (decode_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate decode buffer");
    goto cleanup;
  }

  err = rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(err));
    goto cleanup;
  }

  TickType_t last_hop_time = xTaskGetTickCount();
  const TickType_t hop_interval = pdMS_TO_TICKS(HOP_INTERVAL_MS);

  while (s_is_running) {
    handle_frequency_hopping(&last_hop_time, hop_interval);

    if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(RX_QUEUE_TIMEOUT_MS)) != pdPASS) {
      continue;
    }

    if (rx_data.num_symbols == 0) {
      if (s_is_running &&
          rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config) != ESP_OK) {
        ESP_LOGE(TAG, "rmt_receive failed; stopping RX");
        break;
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

    if (s_is_running &&
        rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config) != ESP_OK) {
      ESP_LOGE(TAG, "rmt_receive failed; stopping RX");
      break;
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
  if (s_rx_channel != NULL) {
    rmt_disable(s_rx_channel);
    rmt_del_channel(s_rx_channel);
    s_rx_channel = NULL;
  }
  if (s_rx_queue != NULL) {
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
  }
  s_is_running = false;
  s_rx_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t subghz_receiver_start(subghz_mode_t mode, cc1101_preset_t preset, uint32_t freq) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  subghz_tx_stop();

  s_rx_mode = mode;
  s_rx_preset = preset;

  if (s_result_mutex == NULL)
    s_result_mutex = xSemaphoreCreateMutex();
  if (s_result_mutex != NULL &&
      xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) == pdTRUE) {
    s_result_seq = 0;
    memset(&s_latest, 0, sizeof(s_latest));
    xSemaphoreGive(s_result_mutex);
  }

  if (freq == 0) {
    s_is_hopping_active = true;
    s_hop_idx = 0;
    s_rx_freq = HOP_FREQUENCIES[0];
  } else {
    s_is_hopping_active = false;
    s_rx_freq = freq;
  }

  BaseType_t ret = xTaskCreatePinnedToCore(subghz_rx_task,
                                           "subghz_rx",
                                           RX_TASK_STACK_SIZE,
                                           NULL,
                                           RX_TASK_PRIORITY,
                                           &s_rx_task_handle,
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

bool subghz_receiver_get_result(subghz_rx_result_t *out) {
  if (out == NULL || s_result_mutex == NULL)
    return false;
  if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) != pdTRUE)
    return false;

  bool has = (s_latest.seq != 0);
  if (has) {
    *out = s_latest;
    out->data.protocol_name = out->name_buf;
  }
  xSemaphoreGive(s_result_mutex);
  return has;
}
