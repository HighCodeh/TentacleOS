// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tos_api_subghz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "cc1101.h"
#include "cc1101_lease.h"
#include "host_link_local_rf.h"
#include "pin_def.h"
#include "power_manager.h"
#include "subghz_protocol_registry.h"
#include "subghz_protocol_serializer.h"
#include "tos_app_ctx.h"
#include "sys_prio.h"

static const char *TAG = "TOS_SUBGHZ";

#define SUBGHZ_SPECTRUM_SPAN_MAX_HZ 20000000
#define SUBGHZ_SPECTRUM_SETTLE_US   400
#define SUBGHZ_SPECTRUM_SAMPLE_US   20
#define SUBGHZ_SPECTRUM_SAMPLES     3
#define SUBGHZ_SPECTRUM_MIN_DBM     (-130.0f)
#define SUBGHZ_SPECTRUM_DIVISOR     2
#define SUBGHZ_STATUS_READ          0x40
#define SUBGHZ_SYMBOLS_MAX          512
#define SUBGHZ_PULSES_PER_SYMBOL    2
#define SUBGHZ_PULSES_MAX           512
#define SUBGHZ_RMT_HZ               1000000
#define SUBGHZ_RMT_RX_BLOCK         256
#define SUBGHZ_RMT_TX_BLOCK         64
#define SUBGHZ_DMA_ALIGNMENT        64
#define SUBGHZ_RX_MIN_NS            1000
#define SUBGHZ_RX_MAX_NS            10000000
#define SUBGHZ_FILTER_US            15
#define SUBGHZ_RX_TIMEOUT_MAX_MS    1000
#define SUBGHZ_TX_MAX_US            2000000
#define SUBGHZ_TX_WAIT_MS           2100
#define SUBGHZ_PULSE_MAX_US         32767
#define SUBGHZ_FILE_MAX_BYTES       8192
#define SUBGHZ_PATH_MAX             192
#define SUBGHZ_SD_PREFIX            "/sdcard/"
#define SUBGHZ_SESSION_STACK_BYTES  4096
#define SUBGHZ_SESSION_WINDOW_MS    100
#define SUBGHZ_SESSION_LOCK_MS      100
#define SUBGHZ_SESSION_RETRY_MS     10
#define SUBGHZ_SESSION_WAIT_MS      3000
#define SUBGHZ_LOCK_MS              3000
#define SUBGHZ_BAND1_MIN_HZ         300000000
#define SUBGHZ_BAND1_MAX_HZ         348000000
#define SUBGHZ_BAND2_MIN_HZ         387000000
#define SUBGHZ_BAND2_MAX_HZ         464000000
#define SUBGHZ_BAND3_MIN_HZ         779000000
#define SUBGHZ_BAND3_MAX_HZ         928000000

typedef struct {
  tos_subghz_signal_t *out_signal;
  tos_subghz_spectrum_t *out_spectrum;
  uint32_t span_hz;
  const int32_t *timings;
  uint32_t frequency;
  uint32_t timeout_ms;
  size_t count;
  uint8_t preset;
  bool has_no_signal;
  bool is_cleanup_complete;
} tos_api_subghz_request_t;

typedef enum {
  TOS_SUBGHZ_SESSION_IDLE = 0,
  TOS_SUBGHZ_SESSION_STARTING,
  TOS_SUBGHZ_SESSION_RUNNING,
  TOS_SUBGHZ_SESSION_STOPPING,
  TOS_SUBGHZ_SESSION_COUNT
} tos_api_subghz_phase_t;

typedef struct {
  uint32_t frequency;
  uint32_t span_hz;
  uint8_t preset;
  bool is_spectrum;
} tos_api_subghz_config_t;

typedef union {
  tos_subghz_signal_t signal;
  tos_subghz_spectrum_t spectrum;
} tos_api_subghz_result_t;

typedef struct {
  TaskHandle_t owner;
  uint32_t id;
  tos_api_subghz_phase_t phase;
  tos_api_subghz_config_t config;
  tos_api_subghz_result_t result;
  esp_err_t error;
  bool has_new;
} tos_api_subghz_session_t;

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static QueueHandle_t s_queue;
static rmt_symbol_word_t *s_symbols;
static int32_t *s_pulses;
static int32_t *s_last_raw;
static size_t s_last_raw_count;
static bool s_is_enabled;
static bool s_has_radio;
static bool s_has_power_hold;
static portMUX_TYPE s_session_lock = portMUX_INITIALIZER_UNLOCKED;
static tos_api_subghz_session_t s_session;
static TaskHandle_t s_session_task;
static uint32_t s_session_sequence;

static bool valid_frequency(uint32_t frequency);
static bool valid_spectrum(uint32_t center_hz, uint32_t span_hz);
static esp_err_t cleanup_hardware(void);
static bool
rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *context);
static esp_err_t receive_once(tos_api_subghz_request_t *request);
static esp_err_t transmit_once(tos_api_subghz_request_t *request);
static esp_err_t sweep_once(tos_api_subghz_request_t *request);
static esp_err_t run_operation(void *context);
static esp_err_t begin_call(uint32_t capability);
static esp_err_t begin_operation(uint32_t capability);
static esp_err_t
transmit_checked(const int32_t *timings, int count, uint32_t frequency, uint8_t preset);
static bool valid_path(const char *path);
static esp_err_t cleanup_session(void *context);
static void session_task(void *context);
static esp_err_t start_session(const tos_api_subghz_config_t *config, uint32_t *out_session);

esp_err_t tos_api_subghz_receive(tos_subghz_signal_t *out_signal,
                                 uint32_t frequency,
                                 uint8_t preset,
                                 uint32_t timeout_ms) {
  if (out_signal == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out_signal, 0, sizeof(*out_signal));
  if (!valid_frequency(frequency) || preset == CC1101_PRESET_IDLE ||
      preset >= CC1101_PRESET_COUNT || timeout_ms == 0 || timeout_ms > SUBGHZ_RX_TIMEOUT_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = begin_operation(TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return error;
  tos_subghz_signal_t signal = {0};
  tos_api_subghz_request_t request = {
      .out_signal = &signal, .frequency = frequency, .preset = preset, .timeout_ms = timeout_ms};
  error = host_local_rf_call(run_operation, &request);
  if (error == ESP_OK)
    *out_signal = signal;
  host_local_rf_unlock();
  return error;
}

esp_err_t tos_api_subghz_tx_raw(const int32_t *timings, int count, uint32_t frequency) {
  if (timings == NULL || count <= 0 || count > SUBGHZ_PULSES_MAX || !valid_frequency(frequency))
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = begin_operation(TOS_CAP_RADIO_TX);
  if (error != ESP_OK)
    return error;
  int32_t *copy = malloc((size_t)count * sizeof(*copy));
  if (copy == NULL) {
    ESP_LOGE(TAG, "Cannot copy TX timings");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  memcpy(copy, timings, (size_t)count * sizeof(*copy));
  error = transmit_checked(copy, count, frequency, CC1101_PRESET_OOK_800KHZ);
cleanup:
  free(copy);
  host_local_rf_unlock();
  return error;
}

esp_err_t tos_api_subghz_spectrum_capture(tos_subghz_spectrum_t *out_spectrum,
                                          uint32_t center_hz,
                                          uint32_t span_hz) {
  if (out_spectrum == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out_spectrum, 0, sizeof(*out_spectrum));
  if (!valid_spectrum(center_hz, span_hz))
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = begin_operation(TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return error;
  tos_subghz_spectrum_t spectrum = {0};
  tos_api_subghz_request_t request = {.out_spectrum = &spectrum,
                                      .frequency = center_hz,
                                      .span_hz = span_hz,
                                      .preset = CC1101_PRESET_OOK_800KHZ};
  error = host_local_rf_call(run_operation, &request);
  if (error == ESP_OK)
    *out_spectrum = spectrum;
  host_local_rf_unlock();
  return error;
}

int tos_api_subghz_get_raw(int32_t *out, int max) {
  if (out == NULL || max <= 0 || max > SUBGHZ_SYMBOLS_MAX * SUBGHZ_PULSES_PER_SYMBOL)
    return -ESP_ERR_INVALID_ARG;
  esp_err_t error = begin_call(TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return -error;
  int count = 0;
  if (s_last_raw_count > (size_t)max) {
    count = -ESP_ERR_INVALID_SIZE;
  } else if (s_last_raw_count != 0) {
    memcpy(out, s_last_raw, s_last_raw_count * sizeof(*out));
    count = (int)s_last_raw_count;
  }
  host_local_rf_unlock();
  return count;
}

esp_err_t tos_api_subghz_replay(const char *path) {
  if (!valid_path(path))
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = begin_operation(TOS_CAP_RADIO_TX | TOS_CAP_FS_READ);
  if (error != ESP_OK)
    return error;
  FILE *file = fopen(path, "rb");
  char *content = NULL;
  int32_t *pulses = NULL;
  if (file == NULL) {
    error = ESP_ERR_NOT_FOUND;
    goto cleanup;
  }
  error = ESP_FAIL;
  if (fseek(file, 0, SEEK_END) != 0)
    goto cleanup;
  long size = ftell(file);
  if (size <= 0 || size > SUBGHZ_FILE_MAX_BYTES) {
    error = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }
  if (fseek(file, 0, SEEK_SET) != 0)
    goto cleanup;
  content = malloc((size_t)size + 1);
  pulses = malloc(SUBGHZ_PULSES_MAX * sizeof(*pulses));
  if (content == NULL || pulses == NULL) {
    ESP_LOGE(TAG, "Cannot allocate replay storage");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  if (fread(content, 1, (size_t)size, file) != (size_t)size || fgetc(file) != EOF || ferror(file))
    goto cleanup;
  if (memchr(content, '\0', (size_t)size) != NULL) {
    error = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  content[size] = '\0';
  size_t count = 0;
  uint32_t frequency = 0;
  uint8_t preset = 0;
  error = subghz_protocol_parse_capture(
      content, pulses, SUBGHZ_PULSES_MAX, &count, &frequency, &preset);
  if (error != ESP_OK)
    goto cleanup;
  if (tos_api_get()->should_stop()) {
    error = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  error = transmit_checked(pulses, (int)count, frequency, preset);
cleanup:
  if (file != NULL)
    fclose(file);
  free(pulses);
  free(content);
  host_local_rf_unlock();
  return error;
}

esp_err_t tos_api_subghz_rx_start(uint32_t frequency, uint8_t preset, uint32_t *out_session) {
  if (out_session != NULL)
    *out_session = 0;
  if (!valid_frequency(frequency) || preset == CC1101_PRESET_IDLE || preset >= CC1101_PRESET_COUNT)
    return ESP_ERR_INVALID_ARG;
  tos_api_subghz_config_t config = {.frequency = frequency, .preset = preset};
  return start_session(&config, out_session);
}

esp_err_t
tos_api_subghz_spectrum_start(uint32_t center_hz, uint32_t span_hz, uint32_t *out_session) {
  if (out_session != NULL)
    *out_session = 0;
  if (!valid_spectrum(center_hz, span_hz))
    return ESP_ERR_INVALID_ARG;
  tos_api_subghz_config_t config = {.frequency = center_hz,
                                    .span_hz = span_hz,
                                    .preset = CC1101_PRESET_OOK_800KHZ,
                                    .is_spectrum = true};
  return start_session(&config, out_session);
}

int tos_api_subghz_session_poll(tos_subghz_signal_t *out, bool *out_has_session) {
  if (out == NULL || out_has_session == NULL)
    return -ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_session_lock);
  *out_has_session = s_session.owner == xTaskGetCurrentTaskHandle() && s_session.id != 0;
  portEXIT_CRITICAL(&s_session_lock);
  if (!*out_has_session)
    return 0;
  esp_err_t error = begin_call(TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return -error;
  portENTER_CRITICAL(&s_session_lock);
  if (s_session.owner != xTaskGetCurrentTaskHandle() || s_session.config.is_spectrum)
    error = ESP_ERR_INVALID_STATE;
  else
    error = s_session.error;
  int count = 0;
  if (error == ESP_OK && s_session.has_new) {
    *out = s_session.result.signal;
    s_session.has_new = false;
    count = 1;
  }
  portEXIT_CRITICAL(&s_session_lock);
  host_local_rf_unlock();
  return error == ESP_OK ? count : (error > ESP_OK ? -error : error);
}

int tos_api_subghz_spectrum_poll(uint32_t *out_start, uint32_t *out_step, float *out_dbm, int max) {
  if (out_start == NULL || out_step == NULL || out_dbm == NULL)
    return -ESP_ERR_INVALID_ARG;
  if (max < TOS_SUBGHZ_SPECTRUM_BINS)
    return -ESP_ERR_INVALID_SIZE;
  esp_err_t error = begin_call(TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return -error;
  int count = 0;
  portENTER_CRITICAL(&s_session_lock);
  if (s_session.owner != xTaskGetCurrentTaskHandle() || !s_session.config.is_spectrum ||
      s_session.id == 0)
    error = ESP_ERR_INVALID_STATE;
  else
    error = s_session.error;
  if (error == ESP_OK && s_session.has_new) {
    *out_start = s_session.result.spectrum.start_hz;
    *out_step = s_session.result.spectrum.step_hz;
    memcpy(out_dbm, s_session.result.spectrum.dbm, sizeof(s_session.result.spectrum.dbm));
    s_session.has_new = false;
    count = TOS_SUBGHZ_SPECTRUM_BINS;
  }
  portEXIT_CRITICAL(&s_session_lock);
  host_local_rf_unlock();
  return error == ESP_OK ? count : (error > ESP_OK ? -error : error);
}

esp_err_t tos_api_subghz_set_frequency(uint32_t frequency) {
  if (!valid_frequency(frequency))
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = begin_call(TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return error;
  portENTER_CRITICAL(&s_session_lock);
  if (s_session.owner != xTaskGetCurrentTaskHandle() || s_session.config.is_spectrum ||
      s_session.phase != TOS_SUBGHZ_SESSION_RUNNING) {
    error = ESP_ERR_INVALID_STATE;
  } else {
    s_session.config.frequency = frequency;
    s_session.has_new = false;
  }
  portEXIT_CRITICAL(&s_session_lock);
  host_local_rf_unlock();
  return error;
}

esp_err_t tos_api_subghz_session_stop(uint32_t session) {
  if (!tos_app_cap_check(TOS_CAP_RADIO_RX))
    return ESP_ERR_NOT_SUPPORTED;
  portENTER_CRITICAL(&s_session_lock);
  bool is_owner =
      session != 0 && s_session.id == session && s_session.owner == xTaskGetCurrentTaskHandle();
  if (is_owner) {
    s_session.has_new = false;
    if (s_session.phase != TOS_SUBGHZ_SESSION_IDLE)
      s_session.phase = TOS_SUBGHZ_SESSION_STOPPING;
  }
  portEXIT_CRITICAL(&s_session_lock);
  if (!is_owner)
    return ESP_ERR_INVALID_STATE;
  for (uint32_t elapsed = 0; elapsed < SUBGHZ_SESSION_WAIT_MS; elapsed += SUBGHZ_SESSION_RETRY_MS) {
    portENTER_CRITICAL(&s_session_lock);
    bool is_done = s_session.id != session || s_session.phase == TOS_SUBGHZ_SESSION_IDLE;
    if (is_done && s_session.id == session)
      s_session.error = ESP_OK;
    portEXIT_CRITICAL(&s_session_lock);
    if (is_done)
      return ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(SUBGHZ_SESSION_RETRY_MS));
  }
  return ESP_ERR_TIMEOUT;
}

void tos_api_subghz_cancel_owner(void *task) {
  if (task == NULL)
    return;
  portENTER_CRITICAL(&s_session_lock);
  if (s_session.owner == task) {
    s_session.owner = NULL;
    s_session.has_new = false;
    if (s_session.phase != TOS_SUBGHZ_SESSION_IDLE)
      s_session.phase = TOS_SUBGHZ_SESSION_STOPPING;
    else
      s_session.id = 0;
  }
  portEXIT_CRITICAL(&s_session_lock);
}

static bool valid_frequency(uint32_t frequency) {
  return (frequency >= SUBGHZ_BAND1_MIN_HZ && frequency <= SUBGHZ_BAND1_MAX_HZ) ||
         (frequency >= SUBGHZ_BAND2_MIN_HZ && frequency <= SUBGHZ_BAND2_MAX_HZ) ||
         (frequency >= SUBGHZ_BAND3_MIN_HZ && frequency <= SUBGHZ_BAND3_MAX_HZ);
}

static bool valid_spectrum(uint32_t center_hz, uint32_t span_hz) {
  if (!valid_frequency(center_hz) || span_hz < TOS_SUBGHZ_SPECTRUM_BINS ||
      span_hz > SUBGHZ_SPECTRUM_SPAN_MAX_HZ)
    return false;
  uint32_t start = center_hz - span_hz / SUBGHZ_SPECTRUM_DIVISOR;
  uint32_t end = start + span_hz;
  if (!((start >= SUBGHZ_BAND1_MIN_HZ && end <= SUBGHZ_BAND1_MAX_HZ) ||
        (start >= SUBGHZ_BAND2_MIN_HZ && end <= SUBGHZ_BAND2_MAX_HZ) ||
        (start >= SUBGHZ_BAND3_MIN_HZ && end <= SUBGHZ_BAND3_MAX_HZ)))
    return false;
  return true;
}

static esp_err_t cleanup_hardware(void) {
  if (s_has_radio) {
    (void)cc1101_lease_take_error();
    cc1101_strobe(CC1101_SIDLE);
    esp_err_t error = cc1101_lease_take_error();
    if (error != ESP_OK)
      return error;
  }
  if (s_channel != NULL) {
    esp_err_t error = s_is_enabled ? rmt_disable(s_channel) : ESP_OK;
    if (error != ESP_OK)
      return error;
    s_is_enabled = false;
    error = rmt_del_channel(s_channel);
    if (error != ESP_OK)
      return error;
    s_channel = NULL;
  }
  if (s_encoder != NULL) {
    esp_err_t error = rmt_del_encoder(s_encoder);
    if (error != ESP_OK)
      return error;
    s_encoder = NULL;
  }
  if (s_queue != NULL) {
    vQueueDelete(s_queue);
    s_queue = NULL;
  }
  free(s_symbols);
  free(s_pulses);
  s_symbols = NULL;
  s_pulses = NULL;
  s_has_radio = false;
  if (s_has_power_hold) {
    esp_err_t error = power_manager_no_sleep_release();
    if (error != ESP_OK)
      return error;
    s_has_power_hold = false;
  }
  return ESP_OK;
}

static bool
rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *context) {
  BaseType_t wake = pdFALSE;
  xQueueSendFromISR((QueueHandle_t)context, event, &wake);
  return wake == pdTRUE;
}

static esp_err_t receive_once(tos_api_subghz_request_t *request) {
  const rmt_rx_channel_config_t config = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                          .resolution_hz = SUBGHZ_RMT_HZ,
                                          .mem_block_symbols = SUBGHZ_RMT_RX_BLOCK,
                                          .gpio_num = GPIO_CC1101_GDO0_PIN,
                                          .flags.with_dma = true};
  esp_err_t error = rmt_new_rx_channel(&config, &s_channel);
  if (error != ESP_OK)
    return error;
  s_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  s_symbols = heap_caps_aligned_alloc(SUBGHZ_DMA_ALIGNMENT,
                                      SUBGHZ_SYMBOLS_MAX * sizeof(*s_symbols),
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  s_pulses = heap_caps_malloc(SUBGHZ_SYMBOLS_MAX * SUBGHZ_PULSES_PER_SYMBOL * sizeof(*s_pulses),
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_queue == NULL || s_symbols == NULL || s_pulses == NULL) {
    ESP_LOGE(TAG, "Cannot allocate capture buffers");
    return ESP_ERR_NO_MEM;
  }
  const rmt_rx_event_callbacks_t callbacks = {.on_recv_done = rx_done};
  error = rmt_rx_register_event_callbacks(s_channel, &callbacks, s_queue);
  if (error != ESP_OK)
    return error;
  error = rmt_enable(s_channel);
  if (error != ESP_OK)
    return error;
  s_is_enabled = true;
  const rmt_receive_config_t receive_config = {.signal_range_min_ns = SUBGHZ_RX_MIN_NS,
                                               .signal_range_max_ns = SUBGHZ_RX_MAX_NS};
  error =
      rmt_receive(s_channel, s_symbols, SUBGHZ_SYMBOLS_MAX * sizeof(*s_symbols), &receive_config);
  if (error != ESP_OK)
    return error;
  rmt_rx_done_event_data_t event;
  TickType_t wait = pdMS_TO_TICKS(request->timeout_ms);
  if (xQueueReceive(s_queue, &event, wait != 0 ? wait : 1) != pdTRUE) {
    request->has_no_signal = true;
    return ESP_ERR_TIMEOUT;
  }
  if (event.num_symbols == 0) {
    request->has_no_signal = true;
    return ESP_ERR_NOT_FOUND;
  }
  if (event.received_symbols != s_symbols || event.num_symbols > SUBGHZ_SYMBOLS_MAX)
    return ESP_ERR_INVALID_SIZE;
  size_t count = 0;
  for (size_t i = 0; i < event.num_symbols; ++i) {
    rmt_symbol_word_t symbol = s_symbols[i];
    if (symbol.duration0 >= SUBGHZ_FILTER_US)
      s_pulses[count++] = symbol.level0 ? (int32_t)symbol.duration0 : -(int32_t)symbol.duration0;
    if (symbol.duration1 >= SUBGHZ_FILTER_US)
      s_pulses[count++] = symbol.level1 ? (int32_t)symbol.duration1 : -(int32_t)symbol.duration1;
  }
  if (count == 0) {
    request->has_no_signal = true;
    return ESP_ERR_NOT_FOUND;
  }
  request->count = count;
  subghz_data_t data = {0};
  tos_subghz_signal_t *out = request->out_signal;
  out->freq_hz = request->frequency;
  out->decoded = subghz_protocol_registry_decode_all(s_pulses, count, &data);
  if (out->decoded) {
    if (data.protocol_name != NULL)
      strlcpy(out->protocol, data.protocol_name, sizeof(out->protocol));
    out->value = data.raw_value;
    out->serial = data.serial;
    out->bit_count = data.bit_count;
    out->btn = data.btn;
  }
  return ESP_OK;
}

static esp_err_t transmit_once(tos_api_subghz_request_t *request) {
  const rmt_tx_channel_config_t config = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                          .resolution_hz = SUBGHZ_RMT_HZ,
                                          .mem_block_symbols = SUBGHZ_RMT_TX_BLOCK,
                                          .gpio_num = GPIO_CC1101_GDO0_PIN,
                                          .trans_queue_depth = 1};
  esp_err_t error = rmt_new_tx_channel(&config, &s_channel);
  if (error != ESP_OK)
    return error;
  const rmt_copy_encoder_config_t encoder = {};
  error = rmt_new_copy_encoder(&encoder, &s_encoder);
  if (error != ESP_OK)
    return error;
  size_t words = (request->count + 1) / SUBGHZ_PULSES_PER_SYMBOL;
  s_symbols = calloc(words, sizeof(*s_symbols));
  if (s_symbols == NULL) {
    ESP_LOGE(TAG, "Cannot allocate TX symbols");
    return ESP_ERR_NO_MEM;
  }
  for (size_t i = 0; i < request->count; ++i) {
    int32_t timing = request->timings[i];
    uint32_t duration = timing > 0 ? timing : -timing;
    if (i % SUBGHZ_PULSES_PER_SYMBOL == 0) {
      s_symbols[i / SUBGHZ_PULSES_PER_SYMBOL].duration0 = duration;
      s_symbols[i / SUBGHZ_PULSES_PER_SYMBOL].level0 = timing > 0;
    } else {
      s_symbols[i / SUBGHZ_PULSES_PER_SYMBOL].duration1 = duration;
      s_symbols[i / SUBGHZ_PULSES_PER_SYMBOL].level1 = timing > 0;
    }
  }
  error = rmt_enable(s_channel);
  if (error != ESP_OK)
    return error;
  s_is_enabled = true;
  cc1101_enter_tx_mode();
  error = cc1101_lease_take_error();
  if (error != ESP_OK)
    return error;
  const rmt_transmit_config_t transmit = {0};
  error = rmt_transmit(s_channel, s_encoder, s_symbols, words * sizeof(*s_symbols), &transmit);
  return error == ESP_OK ? rmt_tx_wait_all_done(s_channel, SUBGHZ_TX_WAIT_MS) : error;
}

static esp_err_t sweep_once(tos_api_subghz_request_t *request) {
  tos_subghz_spectrum_t *spectrum = request->out_spectrum;
  spectrum->start_hz = request->frequency - request->span_hz / SUBGHZ_SPECTRUM_DIVISOR;
  spectrum->step_hz = request->span_hz / TOS_SUBGHZ_SPECTRUM_BINS;
  for (uint32_t i = 0; i < TOS_SUBGHZ_SPECTRUM_BINS; ++i) {
    cc1101_strobe(CC1101_SIDLE);
    cc1101_set_frequency(spectrum->start_hz + i * spectrum->step_hz);
    cc1101_strobe(CC1101_SRX);
    esp_err_t error = cc1101_lease_take_error();
    if (error != ESP_OK)
      return error;
    ets_delay_us(SUBGHZ_SPECTRUM_SETTLE_US);
    float peak = SUBGHZ_SPECTRUM_MIN_DBM;
    for (uint32_t sample = 0; sample < SUBGHZ_SPECTRUM_SAMPLES; ++sample) {
      uint8_t raw = cc1101_read_reg(CC1101_RSSI | SUBGHZ_STATUS_READ);
      error = cc1101_lease_take_error();
      if (error != ESP_OK)
        return error;
      float dbm = cc1101_convert_rssi(raw);
      if (dbm > peak)
        peak = dbm;
      ets_delay_us(SUBGHZ_SPECTRUM_SAMPLE_US);
    }
    spectrum->dbm[i] = peak;
    vTaskDelay(1);
  }
  return ESP_OK;
}

static esp_err_t run_operation(void *context) {
  tos_api_subghz_request_t *request = context;
  int32_t *capture = NULL;
  esp_err_t error = cc1101_lease_acquire();
  if (error != ESP_OK)
    return error;
  error = cleanup_hardware();
  if (error != ESP_OK) {
    cc1101_lease_pause();
    return error;
  }
  error = power_manager_no_sleep_acquire();
  if (error != ESP_OK)
    goto cleanup;
  s_has_power_hold = true;
  s_has_radio = true;
  error = cc1101_recover();
  if (error != ESP_OK)
    goto cleanup;
  cc1101_set_preset(request->preset, request->frequency);
  error = cc1101_lease_take_error();
  if (error != ESP_OK)
    goto cleanup;
  if (request->out_spectrum != NULL)
    error = sweep_once(request);
  else
    error = request->out_signal != NULL ? receive_once(request) : transmit_once(request);
cleanup:;
  if (error == ESP_OK && request->out_signal != NULL) {
    capture = s_pulses;
    s_pulses = NULL;
  }
  esp_err_t cleanup_error = cleanup_hardware();
  if (cleanup_error == ESP_OK)
    cleanup_error = cc1101_lease_release();
  if (cleanup_error != ESP_OK) {
    cc1101_lease_pause();
    ESP_LOGE(TAG, "Cleanup pending on persistent RF worker: %s", esp_err_to_name(cleanup_error));
  }
  request->is_cleanup_complete = cleanup_error == ESP_OK;
  if (capture != NULL && error == ESP_OK && cleanup_error == ESP_OK) {
    free(s_last_raw);
    s_last_raw = capture;
    s_last_raw_count = request->count;
  } else {
    free(capture);
  }
  return cleanup_error != ESP_OK ? cleanup_error : error;
}

static esp_err_t begin_call(uint32_t capability) {
  if (!tos_app_cap_check(capability))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop() || !host_local_rf_is_ready())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(SUBGHZ_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  if (tos_api_get()->should_stop()) {
    host_local_rf_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

static esp_err_t begin_operation(uint32_t capability) {
  esp_err_t error = begin_call(capability);
  if (error != ESP_OK)
    return error;
  portENTER_CRITICAL(&s_session_lock);
  bool is_busy = s_session.phase != TOS_SUBGHZ_SESSION_IDLE;
  portEXIT_CRITICAL(&s_session_lock);
  if (is_busy) {
    host_local_rf_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

static esp_err_t
transmit_checked(const int32_t *timings, int count, uint32_t frequency, uint8_t preset) {
  if (!valid_frequency(frequency) || preset == CC1101_PRESET_IDLE || preset >= CC1101_PRESET_COUNT)
    return ESP_ERR_INVALID_ARG;
  uint32_t duration = 0;
  for (int i = 0; i < count; ++i) {
    if (timings[i] == 0 || timings[i] < -SUBGHZ_PULSE_MAX_US || timings[i] > SUBGHZ_PULSE_MAX_US) {
      return ESP_ERR_INVALID_ARG;
    }
    duration += timings[i] > 0 ? timings[i] : -timings[i];
  }
  if (duration > SUBGHZ_TX_MAX_US) {
    return ESP_ERR_INVALID_ARG;
  }
  tos_api_subghz_request_t request = {
      .timings = timings, .count = count, .frequency = frequency, .preset = preset};
  return host_local_rf_call(run_operation, &request);
}

static bool valid_path(const char *path) {
  if (path == NULL || strlen(path) >= SUBGHZ_PATH_MAX ||
      strncmp(path, SUBGHZ_SD_PREFIX, sizeof(SUBGHZ_SD_PREFIX) - 1) != 0)
    return false;
  const char *part = path + sizeof(SUBGHZ_SD_PREFIX) - 1;
  while (*part != '\0') {
    const char *end = strchr(part, '/');
    size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
    if (length == 0 || (length == 1 && part[0] == '.') ||
        (length == 2 && part[0] == '.' && part[1] == '.') || strchr(part, '\\') != NULL)
      return false;
    if (end == NULL)
      return true;
    part = end + 1;
  }
  return false;
}

static esp_err_t cleanup_session(void *context) {
  (void)context;
  if (!s_has_radio && !s_has_power_hold && s_channel == NULL && s_encoder == NULL &&
      s_queue == NULL && s_symbols == NULL && s_pulses == NULL)
    return ESP_OK;
  esp_err_t error = cc1101_lease_acquire();
  if (error != ESP_OK)
    return error;
  error = cleanup_hardware();
  if (error == ESP_OK)
    error = cc1101_lease_release();
  if (error != ESP_OK)
    cc1101_lease_pause();
  return error;
}

static void session_task(void *context) {
  (void)context;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      portENTER_CRITICAL(&s_session_lock);
      bool is_idle = s_session.phase == TOS_SUBGHZ_SESSION_IDLE;
      portEXIT_CRITICAL(&s_session_lock);
      if (is_idle)
        break;
      if (!host_local_rf_lock(SUBGHZ_SESSION_LOCK_MS)) {
        vTaskDelay(pdMS_TO_TICKS(SUBGHZ_SESSION_RETRY_MS));
        continue;
      }
      portENTER_CRITICAL(&s_session_lock);
      tos_api_subghz_config_t config = s_session.config;
      bool is_stopping = s_session.phase == TOS_SUBGHZ_SESSION_STOPPING;
      portEXIT_CRITICAL(&s_session_lock);
      tos_api_subghz_result_t result = {0};
      tos_api_subghz_request_t request = {.frequency = config.frequency,
                                          .span_hz = config.span_hz,
                                          .preset = config.preset,
                                          .timeout_ms = SUBGHZ_SESSION_WINDOW_MS};
      if (config.is_spectrum)
        request.out_spectrum = &result.spectrum;
      else
        request.out_signal = &result.signal;
      esp_err_t error = is_stopping ? host_local_rf_call(cleanup_session, NULL)
                                    : host_local_rf_call(run_operation, &request);
      portENTER_CRITICAL(&s_session_lock);
      if (s_session.phase == TOS_SUBGHZ_SESSION_STOPPING) {
        if ((is_stopping && error == ESP_OK) || (!is_stopping && request.is_cleanup_complete))
          s_session.phase = TOS_SUBGHZ_SESSION_IDLE;
        else if (error != ESP_OK)
          s_session.error = error;
      } else if (error == ESP_OK || (request.has_no_signal && request.is_cleanup_complete)) {
        s_session.phase = TOS_SUBGHZ_SESSION_RUNNING;
        if (error == ESP_OK) {
          s_session.result = result;
          s_session.has_new = true;
        }
      } else {
        s_session.error = error;
        // Teardown takes this lock before removing the bound context.
        if (s_session.owner != NULL)
          tos_app_ctx_signal_resource_lost(s_session.owner);
        s_session.phase =
            request.is_cleanup_complete ? TOS_SUBGHZ_SESSION_IDLE : TOS_SUBGHZ_SESSION_STOPPING;
      }
      portEXIT_CRITICAL(&s_session_lock);
      host_local_rf_unlock();
      vTaskDelay(pdMS_TO_TICKS(SUBGHZ_SESSION_RETRY_MS));
    }
  }
}

static esp_err_t start_session(const tos_api_subghz_config_t *config, uint32_t *out_session) {
  if (out_session == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_session = 0;
  esp_err_t error = begin_call(TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return error;
  portENTER_CRITICAL(&s_session_lock);
  bool can_start = s_session.phase == TOS_SUBGHZ_SESSION_IDLE && s_session_sequence != UINT32_MAX;
  uint32_t id = can_start ? ++s_session_sequence : 0;
  if (can_start)
    s_session = (tos_api_subghz_session_t){.owner = xTaskGetCurrentTaskHandle(),
                                           .id = id,
                                           .phase = TOS_SUBGHZ_SESSION_STARTING,
                                           .config = *config};
  portEXIT_CRITICAL(&s_session_lock);
  if (!can_start) {
    host_local_rf_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  if (s_session_task == NULL && xTaskCreatePinnedToCore(session_task,
                                                        "subghz_app",
                                                        SUBGHZ_SESSION_STACK_BYTES,
                                                        NULL,
                                                        SYS_PRIO_SERVICE_LO,
                                                        &s_session_task,
                                                        SYS_CORE_RADIO) != pdPASS) {
    ESP_LOGE(TAG, "Cannot allocate Sub-GHz session worker");
    portENTER_CRITICAL(&s_session_lock);
    s_session = (tos_api_subghz_session_t){0};
    portEXIT_CRITICAL(&s_session_lock);
    host_local_rf_unlock();
    return ESP_ERR_NO_MEM;
  }
  xTaskNotifyGive(s_session_task);
  host_local_rf_unlock();
  for (uint32_t elapsed = 0; elapsed < SUBGHZ_SESSION_WAIT_MS; elapsed += SUBGHZ_SESSION_RETRY_MS) {
    portENTER_CRITICAL(&s_session_lock);
    bool is_same = s_session.id == id && s_session.owner == xTaskGetCurrentTaskHandle();
    tos_api_subghz_phase_t phase = s_session.phase;
    error = s_session.error;
    portEXIT_CRITICAL(&s_session_lock);
    if (!is_same)
      return ESP_ERR_INVALID_STATE;
    if (phase == TOS_SUBGHZ_SESSION_RUNNING) {
      *out_session = id;
      tos_app_ctx_clear_resource_lost();
      return ESP_OK;
    }
    if (phase != TOS_SUBGHZ_SESSION_STARTING)
      return error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
    vTaskDelay(pdMS_TO_TICKS(SUBGHZ_SESSION_RETRY_MS));
  }
  portENTER_CRITICAL(&s_session_lock);
  if (s_session.id == id && s_session.phase != TOS_SUBGHZ_SESSION_IDLE) {
    s_session.phase = TOS_SUBGHZ_SESSION_STOPPING;
    s_session.error = ESP_ERR_TIMEOUT;
  }
  portEXIT_CRITICAL(&s_session_lock);
  return ESP_ERR_TIMEOUT;
}
