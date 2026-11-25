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
#include <stdatomic.h>

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "cc1101.h"
#include "cc1101_rf_path.h"
#include "pin_def.h"
#include "led_control.h"
#include "subghz_protocol_registry.h"
#include "subghz_analyzer.h"
#include "subghz_storage.h"
#include "subghz_protocol_serializer.h"
#include "subghz_transmitter.h"
#include "resource_mgr.h"

static const char *TAG = "SUBGHZ_RX";

// Only the worker releases this grant, after RMT and DMA have stopped.
static res_handle_t s_rx_res = RES_HANDLE_NONE;

#define RMT_RESOLUTION_HZ      1000000
#define RX_BUFFER_SIZE         1024
#define DECODE_BUFFER_FACTOR   2
#define MIN_PULSE_NS           1000
#define SOFTWARE_FILTER_US     15
#define MAX_PULSE_NS           30000000
#define RMT_MEM_BLOCK_SYMBOLS  256
#define RMT_DMA_ALIGNMENT      64
#define RX_QUEUE_DEPTH         1
#define RX_QUEUE_TIMEOUT_MS    25
#define HOP_INTERVAL_MS        100
#define HOP_SIGNAL_HOLD_MS     1000
#define HOP_RSSI_THRESHOLD_DBM (-90)
#define CAPTURE_TEXT_SIZE      65536
#define RX_TASK_STACK_SIZE     8192
#define RX_TASK_PRIORITY       SYS_PRIO_SERVICE_HI
#define RX_TASK_CORE           SYS_CORE_RADIO
#define FILENAME_BUF_SIZE      32
#define HEX_BUF_SIZE           65
#define MAX_HEX_BYTES          32
#define RAW_PREVIEW_COUNT      20
#define DEFAULT_FREQ           433920000
#define CLEANUP_RETRY_MS       100
#define BURST_MAX_PULSES       8192
#define BURST_MAX_FRAMES       128
#define BURST_MAX_CANDIDATES   32
#define BURST_MIN_PULSES       8
#define BURST_QUIET_MS         350
#define BURST_MAX_MS           10000
#define RSSI_POLL_MS           50
#define RSSI_NOISE_MARGIN_DB   8
#define RSSI_NOISE_WEIGHT      8
#define RSSI_FLOOR_DBM         (-130)
#define CC1101_STATUS_READ     0x40U

typedef struct {
  subghz_data_t data;
  char name[32];
  uint32_t matches;
} subghz_rx_candidate_t;

typedef struct {
  int32_t pulses[BURST_MAX_PULSES];
  size_t ends[BURST_MAX_FRAMES];
  size_t count;
  size_t frames;
  TickType_t started;
  TickType_t last_activity;
  bool is_limited;
  subghz_rx_candidate_t candidates[BURST_MAX_CANDIDATES];
} subghz_rx_burst_t;

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

typedef enum {
  SUBGHZ_RX_IDLE = 0,
  SUBGHZ_RX_RUNNING,
  SUBGHZ_RX_STOPPING,
} subghz_rx_state_t;

static atomic_uint s_state;
static atomic_int s_error;
static subghz_mode_t s_rx_mode = SUBGHZ_MODE_SCAN;
static cc1101_preset_t s_rx_preset = CC1101_PRESET_OOK_800KHZ;
static cc1101_radio_config_t s_rx_radio;
static uint32_t s_rx_freq = DEFAULT_FREQ;
static bool s_is_hopping_active = false;
static int s_hop_idx = 0;
static char *s_capture_text;
static size_t s_capture_length;
static bool s_discard_capture;

static rmt_channel_handle_t s_rx_channel = NULL;
static QueueHandle_t s_rx_queue = NULL;

#define RESULT_LOCK_TIMEOUT_MS 20
static _Atomic(SemaphoreHandle_t) s_result_mutex;
static subghz_rx_result_t s_latest;
static uint32_t s_result_seq = 0;
static subghz_rx_progress_t s_progress;

static void update_progress(const subghz_rx_burst_t *burst,
                            subghz_rx_phase_t phase,
                            TickType_t now,
                            bool has_rssi,
                            int16_t rssi) {
  if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) != pdTRUE)
    return;
  s_progress.phase = phase;
  s_progress.frequency_hz = s_rx_freq;
  s_progress.elapsed_ms = burst->count ? (now - burst->started) * portTICK_PERIOD_MS : 0;
  s_progress.pulse_count = burst->count;
  s_progress.frame_count = burst->frames;
  s_progress.is_limited = burst->is_limited;
  if (has_rssi) {
    if (s_progress.rssi_count == SUBGHZ_RX_RSSI_SAMPLES) {
      memmove(s_progress.rssi_dbm,
              s_progress.rssi_dbm + 1,
              (SUBGHZ_RX_RSSI_SAMPLES - 1) * sizeof(s_progress.rssi_dbm[0]));
    } else {
      ++s_progress.rssi_count;
    }
    s_progress.rssi_dbm[s_progress.rssi_count - 1] = rssi;
    ++s_progress.rssi_sequence;
  }
  xSemaphoreGive(s_result_mutex);
}

bool subghz_receiver_get_progress(subghz_rx_progress_t *out_progress) {
  if (out_progress == NULL || s_result_mutex == NULL ||
      xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) != pdTRUE)
    return false;
  *out_progress = s_progress;
  xSemaphoreGive(s_result_mutex);
  return true;
}

static void record_failure(esp_err_t error);

static void publish_rx_result(bool decoded,
                              const subghz_data_t *data,
                              const subghz_analyzer_result_t *analysis,
                              const char *save_name,
                              uint32_t matching_frames) {
  if (s_result_mutex == NULL)
    return;
  if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) != pdTRUE)
    return;

  if (s_discard_capture) {
    xSemaphoreGive(s_result_mutex);
    return;
  }
  s_result_seq++;
  if (s_result_seq == 0)
    ++s_result_seq;
  s_latest.seq = s_result_seq;
  s_latest.decoded = decoded;
  s_latest.freq = s_rx_freq;
  s_latest.is_limited = s_progress.is_limited;
  s_latest.pulse_count = s_progress.pulse_count;
  s_latest.frame_count = s_progress.frame_count;
  s_latest.matching_frames = matching_frames;
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

static bool
retain_capture(const int32_t *pulses, size_t count, const subghz_data_t *data, uint32_t te) {
  char *text = heap_caps_malloc(CAPTURE_TEXT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (text == NULL) {
    ESP_LOGE(TAG, "Cannot allocate pending capture");
    record_failure(ESP_ERR_NO_MEM);
    return false;
  }
  size_t length =
      data != NULL
          ? subghz_protocol_serialize_decoded(data, s_rx_freq, te, text, CAPTURE_TEXT_SIZE)
          : subghz_protocol_serialize_raw(pulses, count, s_rx_freq, text, CAPTURE_TEXT_SIZE);
  if (length == 0 || length >= CAPTURE_TEXT_SIZE) {
    free(text);
    record_failure(ESP_ERR_INVALID_SIZE);
    return false;
  }
  text[length] = '\0';
  if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) != pdTRUE) {
    free(text);
    record_failure(ESP_ERR_TIMEOUT);
    return false;
  }
  bool is_retained = !s_discard_capture;
  if (is_retained) {
    free(s_capture_text);
    s_capture_text = text;
    s_capture_length = length;
  } else {
    free(text);
  }
  xSemaphoreGive(s_result_mutex);
  return is_retained;
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
  ESP_LOGI(TAG, "RAW captured: %u pulses", (unsigned)decode_idx);
  if (retain_capture(decode_buffer, decode_idx, NULL, 0)) {
    publish_rx_result(false, NULL, NULL, "RAW", 0);
    led_signal_info();
  }
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

static bool decode_burst(subghz_rx_burst_t *burst, subghz_data_t *out, uint32_t *out_matches) {
  size_t candidates = 0;
  size_t start = 0;
  memset(burst->candidates, 0, sizeof(burst->candidates));
  for (size_t frame = 0; frame < burst->frames; ++frame) {
    if (s_state != SUBGHZ_RX_RUNNING)
      return false;
    subghz_data_t data = {0};
    size_t end = burst->ends[frame];
    bool decoded = subghz_protocol_registry_decode_all(burst->pulses + start, end - start, &data);
    start = end;
    if (!decoded || data.protocol_name == NULL)
      continue;
    size_t candidate = 0;
    for (; candidate < candidates; ++candidate) {
      subghz_rx_candidate_t *entry = &burst->candidates[candidate];
      if (entry->data.raw_value == data.raw_value && entry->data.bit_count == data.bit_count &&
          entry->data.serial == data.serial && entry->data.btn == data.btn &&
          strcmp(entry->name, data.protocol_name) == 0)
        break;
    }
    if (candidate == candidates) {
      if (candidates == BURST_MAX_CANDIDATES)
        continue;
      subghz_rx_candidate_t *entry = &burst->candidates[candidates++];
      entry->data = data;
      strlcpy(entry->name, data.protocol_name, sizeof(entry->name));
      entry->data.protocol_name = entry->name;
    }
    ++burst->candidates[candidate].matches;
  }
  size_t best = 0;
  bool is_tie = false;
  for (size_t i = 1; i < candidates; ++i) {
    if (burst->candidates[i].matches > burst->candidates[best].matches) {
      best = i;
      is_tie = false;
    } else if (burst->candidates[i].matches == burst->candidates[best].matches) {
      is_tie = true;
    }
  }
  if (candidates && !is_tie) {
    *out = burst->candidates[best].data;
    *out_matches = burst->candidates[best].matches;
    return true;
  }
  // Whole-buffer fallback can recover a frame split across RMT blocks, but not a conflicting vote.
  if (!candidates && subghz_protocol_registry_decode_all(burst->pulses, burst->count, out)) {
    *out_matches = 1;
    return true;
  }
  return false;
}

static void handle_scan_mode(subghz_rx_burst_t *burst) {
  const int32_t *decode_buffer = burst->pulses;
  size_t decode_idx = burst->count;
  uint32_t matches = 0;
  subghz_data_t decoded = {0};

  if (decode_burst(burst, &decoded, &matches)) {
    ESP_LOGI(TAG,
             "DECODED: Protocol: %s | Serial: 0x%lX | Btn: 0x%X | Bits: %d | Freq: %lu",
             decoded.protocol_name,
             decoded.serial,
             decoded.btn,
             decoded.bit_count,
             (unsigned long)s_rx_freq);

    subghz_analyzer_result_t analysis = {0};
    subghz_analyzer_process(decode_buffer, decode_idx, &analysis);
    if (s_state != SUBGHZ_RX_RUNNING)
      return;
    if (retain_capture(decode_buffer, decode_idx, &decoded, analysis.estimated_te)) {
      publish_rx_result(true, &decoded, &analysis, "Signal", matches);
      led_signal_info();
    }
    return;
  }

  subghz_analyzer_result_t analysis = {0};
  subghz_analyzer_process(decode_buffer, decode_idx, &analysis);
  if (s_state == SUBGHZ_RX_RUNNING) {
    ESP_LOGW(TAG,
             "Unknown Signal: TE ~%luus | Peaks: %s | Count: %d | Freq: %lu",
             (unsigned long)analysis.estimated_te,
             analysis.modulation_hint != NULL ? analysis.modulation_hint : "Unknown",
             (int)decode_idx,
             (unsigned long)s_rx_freq);

    if (retain_capture(decode_buffer, decode_idx, NULL, 0))
      publish_rx_result(false, NULL, &analysis, "RAW", 0);
    log_recovered_bitstream(&analysis);
    led_signal_warning(); // captured RF but no known protocol matched
  }

  size_t preview = decode_idx > RAW_PREVIEW_COUNT ? RAW_PREVIEW_COUNT : decode_idx;
  ESP_LOGD(TAG, "RAW preview (%d of %d pulses):", (int)preview, (int)decode_idx);
  for (size_t k = 0; k < preview; k++) {
    ESP_LOGD(TAG, "  [%d] %ld", (int)k, (long)decode_buffer[k]);
  }
}

static esp_err_t handle_frequency_hopping(TickType_t *last_hop_time) {
  uint32_t next_index = (s_hop_idx + 1) % HOP_FREQUENCIES_COUNT;
  uint32_t frequency = HOP_FREQUENCIES[next_index];
  esp_err_t error = cc1101_set_frequency(frequency);
  if (error == ESP_OK)
    error = cc1101_enter_rx_mode();
  if (error != ESP_OK)
    return error;
  s_hop_idx = next_index;
  s_rx_freq = frequency;
  *last_hop_time = xTaskGetTickCount();
  ESP_LOGD(TAG, "Hopping to: %lu Hz", (unsigned long)frequency);
  return ESP_OK;
}

static void record_failure(esp_err_t error) {
  int expected = ESP_OK;
  if (error != ESP_OK && atomic_compare_exchange_strong(&s_error, &expected, error)) {
    ESP_LOGE(TAG, "Reception stopped: %s", esp_err_to_name(error));
    led_signal_error();
  }
}

static void subghz_rx_task(void *pvParameters) {
  ESP_LOGI(TAG,
           "Starting RMT RX Task - Mode: %s, Preset: %d, Freq: %lu",
           s_rx_mode == SUBGHZ_MODE_RAW ? "RAW" : "SCAN",
           (int)s_rx_preset,
           (unsigned long)s_rx_freq);

  rmt_symbol_word_t *raw_symbols = NULL;
  subghz_rx_burst_t *burst = NULL;
  int32_t *decode_buffer = NULL;
  esp_err_t err = ESP_OK;
  bool is_channel_enabled = false;
  bool has_radio_config = false;

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
  if (s_rx_queue == NULL) {
    ESP_LOGE(TAG, "Failed to allocate RX queue");
    err = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  rmt_rx_event_callbacks_t cbs = {
      .on_recv_done = subghz_rx_done_callback,
  };
  err = rmt_rx_register_event_callbacks(s_rx_channel, &cbs, s_rx_queue);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_rx_register_event_callbacks failed: %s", esp_err_to_name(err));
    goto cleanup;
  }
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
    err = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  burst = heap_caps_malloc(sizeof(*burst), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (burst == NULL) {
    ESP_LOGE(TAG, "Failed to allocate decode buffer");
    err = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  memset(burst, 0, sizeof(*burst));
  decode_buffer = burst->pulses;
  if (s_state != SUBGHZ_RX_RUNNING)
    goto cleanup;
  err = cc1101_init();
  if (err != ESP_OK)
    goto cleanup;
  has_radio_config = true;
  err = cc1101_set_radio_config(&s_rx_radio, s_rx_freq);
  if (err != ESP_OK)
    goto cleanup;
  err = rmt_enable(s_rx_channel);
  if (err != ESP_OK)
    goto cleanup;
  is_channel_enabled = true;
  ESP_LOGI(TAG, "Waiting for signals...");

  err = rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(err));
    goto cleanup;
  }

  TickType_t last_hop_time = xTaskGetTickCount();
  const TickType_t hop_interval = pdMS_TO_TICKS(HOP_INTERVAL_MS);
  TickType_t last_rssi = xTaskGetTickCount() - pdMS_TO_TICKS(RSSI_POLL_MS);
  int16_t noise_floor = RSSI_FLOOR_DBM;
  bool has_noise_floor = false;
  bool is_signal_hold = false;
  TickType_t hold_started = last_hop_time;

  while (s_state == SUBGHZ_RX_RUNNING) {
    TickType_t now = xTaskGetTickCount();
    if (now - last_rssi >= pdMS_TO_TICKS(RSSI_POLL_MS)) {
      uint8_t raw_rssi;
      err = cc1101_get_register(CC1101_RSSI | CC1101_STATUS_READ, &raw_rssi);
      if (err != ESP_OK)
        break;
      int16_t rssi = (int16_t)cc1101_convert_rssi(raw_rssi);
      if (s_is_hopping_active && !burst->count && !is_signal_hold &&
          rssi > HOP_RSSI_THRESHOLD_DBM) {
        is_signal_hold = true;
        hold_started = now;
      }
      if (!has_noise_floor) {
        noise_floor = rssi;
        has_noise_floor = true;
      } else if (burst->count == 0) {
        noise_floor = (noise_floor * (RSSI_NOISE_WEIGHT - 1) + rssi) / RSSI_NOISE_WEIGHT;
      }
      if (burst->count && rssi > noise_floor + RSSI_NOISE_MARGIN_DB)
        burst->last_activity = now;
      update_progress(burst,
                      burst->count ? SUBGHZ_RX_PHASE_CAPTURING : SUBGHZ_RX_PHASE_WAITING,
                      now,
                      true,
                      rssi);
      last_rssi = now;
    }
    if (burst->count && s_rx_mode == SUBGHZ_MODE_SCAN &&
        (burst->is_limited || now - burst->started >= pdMS_TO_TICKS(BURST_MAX_MS) ||
         now - burst->last_activity >= pdMS_TO_TICKS(BURST_QUIET_MS))) {
      burst->is_limited |= now - burst->started >= pdMS_TO_TICKS(BURST_MAX_MS);
      err = rmt_disable(s_rx_channel);
      if (err != ESP_OK)
        break;
      is_channel_enabled = false;
      update_progress(burst, SUBGHZ_RX_PHASE_ANALYZING, now, false, 0);
      if (s_state == SUBGHZ_RX_RUNNING)
        handle_scan_mode(burst);
      update_progress(burst, SUBGHZ_RX_PHASE_COMPLETE, now, false, 0);
      break;
    }
    if (!burst->count && s_is_hopping_active && uxQueueMessagesWaiting(s_rx_queue) == 0 &&
        now - last_hop_time >= hop_interval &&
        (!is_signal_hold || now - hold_started >= pdMS_TO_TICKS(HOP_SIGNAL_HOLD_MS))) {
      err = rmt_disable(s_rx_channel);
      if (err != ESP_OK)
        break;
      is_channel_enabled = false;
      while (xQueueReceive(s_rx_queue, &rx_data, 0) == pdPASS) {}
      err = handle_frequency_hopping(&last_hop_time);
      has_noise_floor = false;
      is_signal_hold = false;
      last_rssi = xTaskGetTickCount();
      if (err != ESP_OK)
        break;
      if (s_state != SUBGHZ_RX_RUNNING)
        break;
      err = rmt_enable(s_rx_channel);
      if (err != ESP_OK)
        break;
      is_channel_enabled = true;
      err = rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config);
      if (err != ESP_OK)
        break;
    }

    if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(RX_QUEUE_TIMEOUT_MS)) != pdPASS) {
      continue;
    }
    if (s_state != SUBGHZ_RX_RUNNING)
      break;

    if (rx_data.num_symbols == 0) {
      if (s_state == SUBGHZ_RX_RUNNING &&
          (err = rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config)) !=
              ESP_OK) {
        ESP_LOGE(TAG, "rmt_receive failed; stopping RX");
        break;
      }
      continue;
    }

    if (rx_data.num_symbols > RX_BUFFER_SIZE) {
      err = ESP_ERR_INVALID_SIZE;
      break;
    }
    size_t offset = s_rx_mode == SUBGHZ_MODE_SCAN ? burst->count : 0;
    size_t decode_idx = decode_rmt_symbols(rx_data.received_symbols,
                                           rx_data.num_symbols,
                                           decode_buffer + offset,
                                           BURST_MAX_PULSES - offset);
    if (s_rx_mode == SUBGHZ_MODE_RAW) {
      if (decode_idx > 0) {
        burst->count = decode_idx;
        burst->frames = 1;
        update_progress(burst, SUBGHZ_RX_PHASE_COMPLETE, now, false, 0);
        handle_raw_mode(decode_buffer, decode_idx);
        break;
      }
    } else if (decode_idx && (burst->count || decode_idx >= BURST_MIN_PULSES)) {
      now = xTaskGetTickCount();
      if (!burst->count)
        burst->started = now;
      burst->count += decode_idx;
      burst->ends[burst->frames++] = burst->count;
      burst->last_activity = now;
      burst->is_limited = burst->count == BURST_MAX_PULSES || burst->frames == BURST_MAX_FRAMES;
      update_progress(burst, SUBGHZ_RX_PHASE_CAPTURING, now, false, 0);
      if (burst->is_limited)
        continue;
    }

    if (s_state == SUBGHZ_RX_RUNNING &&
        (err = rmt_receive(s_rx_channel, raw_symbols, raw_symbols_size, &receive_config)) !=
            ESP_OK) {
      ESP_LOGE(TAG, "rmt_receive failed; stopping RX");
      break;
    }
  }

cleanup:
  s_state = SUBGHZ_RX_STOPPING;
  record_failure(err);
  while (s_rx_channel != NULL) {
    err = is_channel_enabled ? rmt_disable(s_rx_channel) : ESP_OK;
    if (err == ESP_OK) {
      is_channel_enabled = false;
      err = rmt_del_channel(s_rx_channel);
      if (err == ESP_OK) {
        s_rx_channel = NULL;
        break;
      }
    }
    record_failure(err);
    ESP_LOGD(TAG, "RMT cleanup pending: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(CLEANUP_RETRY_MS));
  }
  while (has_radio_config && (err = cc1101_set_preset(CC1101_PRESET_IDLE, 0)) != ESP_OK) {
    record_failure(err);
    vTaskDelay(pdMS_TO_TICKS(CLEANUP_RETRY_MS));
  }
  if (raw_symbols != NULL) {
    free(raw_symbols);
  }
  if (burst != NULL) {
    free(burst);
  }
  if (s_rx_queue != NULL) {
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
  }
  resource_release(s_rx_res); // covers a self-exit that never went through _stop
  s_rx_res = RES_HANDLE_NONE;
  s_state = SUBGHZ_RX_IDLE;
  vTaskDelete(NULL);
}

esp_err_t subghz_receiver_start(subghz_mode_t mode, cc1101_preset_t preset, uint32_t freq) {
  cc1101_radio_config_t config;
  esp_err_t error = cc1101_config_from_preset(preset, &config);
  return error == ESP_OK ? subghz_receiver_start_configured(mode, &config, freq) : error;
}

esp_err_t subghz_receiver_start_configured(subghz_mode_t mode,
                                           const cc1101_radio_config_t *config,
                                           uint32_t freq) {
  if (cc1101_config_validate(config) != ESP_OK)
    return ESP_ERR_INVALID_ARG;
  cc1101_preset_t preset = config->preset;
  if (mode < SUBGHZ_MODE_SCAN || mode >= SUBGHZ_MODE_COUNT || preset <= CC1101_PRESET_IDLE ||
      preset >= CC1101_PRESET_COUNT ||
      (freq != 0 && cc1101_rf_path_for_frequency(freq) == CC1101_RF_PATH_NONE))
    return ESP_ERR_INVALID_ARG;
  unsigned int expected = SUBGHZ_RX_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_RX_RUNNING)) {
    return ESP_ERR_INVALID_STATE;
  }

  atomic_store(&s_error, ESP_OK);
  subghz_tx_stop();

  res_request_t res_req = {.id = RES_SUBGHZ,
                           .lane = RES_LANE_MAIN,
                           .owner_kind = RES_OWNER_UI,
                           .owner_task = NULL,
                           .allow_preempt = false};
  if (resource_acquire(&res_req, &s_rx_res) != ESP_OK) {
    ESP_LOGW(TAG, "SubGhz radio busy; RX not started");
    s_state = SUBGHZ_RX_IDLE;
    return ESP_ERR_INVALID_STATE;
  }

  s_rx_mode = mode;
  s_rx_preset = preset;
  s_rx_radio = *config;

  if (s_result_mutex == NULL)
    s_result_mutex = xSemaphoreCreateMutex();
  if (s_result_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to allocate RX result mutex");
    resource_release(s_rx_res);
    s_rx_res = RES_HANDLE_NONE;
    s_state = SUBGHZ_RX_IDLE;
    return ESP_ERR_NO_MEM;
  }
  if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(RESULT_LOCK_TIMEOUT_MS)) != pdTRUE) {
    resource_release(s_rx_res);
    s_rx_res = RES_HANDLE_NONE;
    s_state = SUBGHZ_RX_IDLE;
    return ESP_ERR_TIMEOUT;
  }
  free(s_capture_text);
  s_capture_text = NULL;
  s_capture_length = 0;
  s_discard_capture = false;
  memset(&s_latest, 0, sizeof(s_latest));
  memset(&s_progress, 0, sizeof(s_progress));
  xSemaphoreGive(s_result_mutex);

  if (freq == 0) {
    s_is_hopping_active = true;
    s_hop_idx = 0;
    s_rx_freq = HOP_FREQUENCIES[0];
  } else {
    s_is_hopping_active = false;
    s_rx_freq = freq;
  }

  BaseType_t ret = xTaskCreatePinnedToCore(
      subghz_rx_task, "subghz_rx", RX_TASK_STACK_SIZE, NULL, RX_TASK_PRIORITY, NULL, RX_TASK_CORE);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create RX task");
    resource_release(s_rx_res);
    s_rx_res = RES_HANDLE_NONE;
    s_state = SUBGHZ_RX_IDLE;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void subghz_receiver_stop(void) {
  unsigned int expected = SUBGHZ_RX_RUNNING;
  atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_RX_STOPPING);
}

esp_err_t subghz_receiver_get_error(void) {
  return atomic_load(&s_error);
}

bool subghz_receiver_is_running(void) {
  return s_state != SUBGHZ_RX_IDLE;
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

void subghz_receiver_discard(void) {
  if (s_result_mutex == NULL)
    return;
  xSemaphoreTake(s_result_mutex, portMAX_DELAY);
  s_discard_capture = true;
  free(s_capture_text);
  s_capture_text = NULL;
  s_capture_length = 0;
  s_latest.seq = 0;
  xSemaphoreGive(s_result_mutex);
}

esp_err_t subghz_receiver_copy_capture(uint32_t sequence, char **out_text) {
  if (out_text == NULL || s_result_mutex == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_text = NULL;
  xSemaphoreTake(s_result_mutex, portMAX_DELAY);
  esp_err_t error = ESP_ERR_INVALID_STATE;
  if (sequence != 0 && sequence == s_latest.seq && s_capture_text != NULL) {
    *out_text = heap_caps_malloc(s_capture_length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (*out_text != NULL) {
      memcpy(*out_text, s_capture_text, s_capture_length + 1);
      error = ESP_OK;
    } else {
      ESP_LOGE(TAG, "Cannot copy pending capture");
      error = ESP_ERR_NO_MEM;
    }
  }
  xSemaphoreGive(s_result_mutex);
  return error;
}

esp_err_t subghz_receiver_save(uint32_t sequence, const char *name) {
  char *text = NULL;
  esp_err_t error = subghz_receiver_copy_capture(sequence, &text);
  if (error == ESP_OK)
    error = subghz_storage_save_capture(name, text);
  free(text);
  return error;
}
