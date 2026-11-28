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

// Host-only hardware fakes; the receiver implementation itself is included unchanged.
#include "rx_fakes.h"

static size_t copy_string(char *out, const char *text, size_t size) {
  size_t length = strlen(text);
  if (size != 0) {
    size_t copied = length < size - 1 ? length : size - 1;
    memcpy(out, text, copied);
    out[copied] = '\0';
  }
  return length;
}

static void *s_dma;
static void *s_decode;
static void release_memory(void *pointer);
static void *allocate_memory(size_t size);
#ifdef strlcpy
#undef strlcpy
#endif
#define strlcpy copy_string
#define free release_memory
#define malloc allocate_memory
#include "subghz_receiver.c"
#undef malloc
#undef free

typedef enum {
  FAIL_NONE, FAIL_MUTEX, FAIL_QUEUE, FAIL_TASK, FAIL_CHANNEL, FAIL_CALLBACK,
  FAIL_ENABLE, FAIL_DMA, FAIL_DECODE, FAIL_RECEIVE, FAIL_RESOURCE,
  FAIL_RADIO_INIT, FAIL_PRESET, FAIL_HOP, FAIL_RX_ENTRY
} failure_t;
static failure_t s_failure;
static void (*s_worker)(void *);
static unsigned int s_disable_failures, s_delete_failures, s_retries;
static unsigned int s_tx_stops, s_receive_calls, s_queue_events;
static bool s_has_resource, s_has_channel, s_has_queue, s_is_enabled;
static bool s_inject_stop, s_expect_dma, s_test_hopping;
static unsigned s_clock_calls, s_stale_events, s_idle_failures;
static bool s_fail_capture;
static unsigned s_hop_case, s_hops, s_first_hop;
static unsigned s_burst_case, s_burst_blocks, s_decodes, s_saves;
static TickType_t s_now;

static void *allocate_memory(size_t size) {
  if (s_failure == FAIL_DECODE)
    return NULL;
  s_decode = malloc(size);
  assert(s_decode != NULL);
  return s_decode;
}

static void release_memory(void *pointer) {
  if (pointer == s_dma || pointer == s_decode) assert(!s_has_channel && !s_is_enabled);
  if (pointer == s_dma)
    s_dma = NULL;
  if (pointer == s_decode)
    s_decode = NULL;
  free(pointer);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  return s_failure == FAIL_MUTEX ? NULL : (void *)1;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) {
  assert(mutex != NULL);
  return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(mutex != NULL); }
QueueHandle_t xQueueCreate(unsigned int count, size_t size) {
  s_has_queue = s_failure != FAIL_QUEUE;
  return s_has_queue ? (void *)2 : NULL;
}
void vQueueDelete(QueueHandle_t queue) {
  assert(s_has_queue && !s_has_channel && !s_is_enabled);
  s_has_queue = false;
}
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *event, BaseType_t *wake) {
  assert(s_has_queue && queue != NULL);
  ++s_queue_events;
  return pdTRUE;
}
unsigned uxQueueMessagesWaiting(QueueHandle_t queue) { return 0; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *out, TickType_t timeout) {
  assert(s_has_queue && s_has_resource);
  if (timeout == 0) {
    if (s_stale_events != 0) {
      --s_stale_events;
      memset(out, 0, sizeof(rmt_rx_done_event_data_t));
      return pdTRUE;
    }
    return pdFALSE;
  }
  if (s_hop_case) {
    s_now += RX_QUEUE_TIMEOUT_MS;
    if (s_now >= 1300) subghz_receiver_stop();
    return pdFALSE;
  }
  if (s_burst_case >= 10) {
    s_now += RX_QUEUE_TIMEOUT_MS;
    if (s_burst_blocks && !s_progress.rejected_bursts)
      return pdFALSE;
    if (s_burst_case == 11 && s_progress.rejected_bursts) {
      subghz_receiver_stop();
      return pdFALSE;
    }
    if (s_burst_blocks >= 4)
      return pdFALSE;
    rmt_symbol_word_t *symbols = s_dma;
    for (unsigned i = 0; i < 16; ++i)
      symbols[i] = (rmt_symbol_word_t){.duration0 = s_burst_blocks ? 350 : 33,
                                      .duration1 = s_burst_blocks ? 700 : 3000,
                                      .level0 = true};
    *(rmt_rx_done_event_data_t *)out = (rmt_rx_done_event_data_t){
        .received_symbols = symbols, .num_symbols = 16};
    ++s_burst_blocks;
    return pdTRUE;
  }
  if (s_burst_case) {
    s_now += RX_QUEUE_TIMEOUT_MS;
    if ((s_burst_case == 5 && s_burst_blocks == 3) ||
        (s_burst_case == 6 && s_burst_blocks == 1)) {
      subghz_receiver_finish();
      return pdFALSE;
    }
    if (s_burst_case == 4 && s_burst_blocks == 2) {
      subghz_receiver_stop();
      return pdFALSE;
    }
    unsigned blocks = s_burst_case == 2 ? BURST_MAX_FRAMES : 3;
    if (s_burst_blocks < blocks) {
      assert(!s_decodes && !s_saves);
      rmt_symbol_word_t *symbols = s_dma;
      unsigned symbols_count = s_burst_case == 6 ? RX_BUFFER_SIZE + 1 :
                               s_burst_case == 7 ? RAW_RX_BUFFER_SIZE : 16;
      for (unsigned i = 0; i < symbols_count; ++i)
        symbols[i] = (rmt_symbol_word_t){.duration0 = s_burst_blocks ? 350 : 700,
                                        .duration1 = 350, .level0 = true};
      *(rmt_rx_done_event_data_t *)out = (rmt_rx_done_event_data_t){.received_symbols=symbols, .num_symbols=symbols_count};
      ++s_burst_blocks;
      return pdTRUE;
    }
    return pdFALSE;
  }
  subghz_receiver_stop();
  assert(subghz_receiver_is_running());
  return pdFALSE;
}
BaseType_t xTaskCreatePinnedToCore(void (*worker)(void *), const char *name,
                                uint32_t stack, void *argument, int priority,
                                TaskHandle_t *handle, int core) {
  assert(priority == SYS_PRIO_SERVICE_HI && core == SYS_CORE_RADIO);
  assert(subghz_receiver_is_running() && s_has_resource);
  unsigned int stops = s_tx_stops;
  assert(subghz_receiver_start(SUBGHZ_MODE_SCAN, 3, DEFAULT_FREQ) == ESP_ERR_INVALID_STATE);
  assert(s_tx_stops == stops);
  if (s_inject_stop)
    subghz_receiver_stop();
  if (s_failure == FAIL_TASK)
    return pdFALSE;
  s_worker = worker;
  return pdPASS;
}
void vTaskDelete(TaskHandle_t task) {
  assert(!s_has_resource && !s_has_channel && !s_has_queue);
  assert(s_dma == NULL && s_decode == NULL && !subghz_receiver_is_running());
}
void vTaskDelay(TickType_t ticks) {
  if (s_rx_mode == SUBGHZ_MODE_RAW && s_state == SUBGHZ_RX_RUNNING) {
    s_now += 100;
    if (s_finish_requested) return;
    if (s_burst_blocks >= 160) { subghz_receiver_finish(); return; }
    if (s_burst_case == 4 && s_burst_blocks == 2) { subghz_receiver_stop(); return; }
    rmt_symbol_word_t *symbols = s_dma;
    for (unsigned i = 0; i < RAW_RX_BUFFER_SIZE; ++i)
      symbols[i] = (rmt_symbol_word_t){.duration0=350, .duration1=700, .level0=true};
    rmt_rx_done_event_data_t event = {.received_symbols=symbols, .num_symbols=RAW_RX_BUFFER_SIZE,
                                      .flags.is_last=(s_burst_blocks & 1) == 0};
    subghz_rx_done_callback(s_rx_channel, &event, s_rx_queue);
    if (s_burst_case == 7)
      for (unsigned i=0; i<RAW_RING_BLOCKS; ++i)
        subghz_rx_done_callback(s_rx_channel, &event, s_rx_queue);
    memset(symbols, 0, RAW_RX_BUFFER_SIZE * sizeof(*symbols));
    ++s_burst_blocks;
    return;
  }

  if (s_burst_case >= 10 && ticks == 1) { ++s_now; return; }
  assert(ticks == CLEANUP_RETRY_MS && s_has_resource);
  assert(subghz_receiver_is_running());
  if (s_expect_dma)
    assert(s_dma != NULL && s_decode != NULL && s_has_queue);
  unsigned int stops = s_tx_stops;
  assert(subghz_receiver_start(SUBGHZ_MODE_SCAN, 3, DEFAULT_FREQ) == ESP_ERR_INVALID_STATE);
  assert(stops == s_tx_stops);
  if (s_is_enabled) {
    rmt_rx_done_event_data_t event = {0};
    subghz_rx_done_callback(s_rx_channel, &event, s_rx_queue);
  }
  ++s_retries;
}
TickType_t xTaskGetTickCount(void) { if (s_burst_case || s_hop_case) return s_now; return s_test_hopping ? s_clock_calls++ * (HOP_INTERVAL_MS + 1) : 0; }
static size_t s_dma_capacity;
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config, rmt_channel_handle_t *out) {
  assert(config->flags.with_dma);
  assert(config->mem_block_symbols == (s_rx_mode == SUBGHZ_MODE_RAW ? RAW_RX_BUFFER_SIZE : RX_BUFFER_SIZE));
  s_dma_capacity = config->mem_block_symbols * sizeof(rmt_symbol_word_t);
  if (s_failure == FAIL_CHANNEL)
    return ESP_FAIL;
  s_has_channel = true;
  *out = (void *)3;
  return ESP_OK;
}
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t channel,
                                        const rmt_rx_event_callbacks_t *callbacks, void *user) {
  assert(s_has_queue && user != NULL);
  return s_failure == FAIL_CALLBACK ? ESP_FAIL : ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t channel) {
  if (s_failure == FAIL_ENABLE)
    return ESP_FAIL;
  s_is_enabled = true;
  return ESP_OK;
}
esp_err_t rmt_disable(rmt_channel_handle_t channel) {
  assert(s_is_enabled && s_has_resource);
  if (s_disable_failures != 0) {
    --s_disable_failures;
    return ESP_FAIL;
  }
  s_is_enabled = false;
  return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t channel) {
  assert(!s_is_enabled && s_has_resource);
  if (s_delete_failures != 0) {
    --s_delete_failures;
    return ESP_FAIL;
  }
  s_has_channel = false;
  return ESP_OK;
}
esp_err_t rmt_receive(rmt_channel_handle_t channel, void *data, size_t size,
                      const rmt_receive_config_t *config) {
  assert(s_is_enabled && s_dma == data && s_decode != NULL);
  assert(size <= s_dma_capacity && size % RMT_DMA_ALIGNMENT == 0);
  assert((uintptr_t)data % RMT_DMA_ALIGNMENT == 0);
  ++s_receive_calls;
  if (s_burst_case == 12 && s_progress.rejected_bursts) return ESP_FAIL;
  return s_failure == FAIL_RECEIVE ? ESP_FAIL : ESP_OK;
}
void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned int caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  assert(alignment == RMT_DMA_ALIGNMENT && size == s_dma_capacity);
  if (s_failure == FAIL_DMA)
    return NULL;
  s_dma = aligned_alloc(alignment, size);
  assert(s_dma != NULL);
  return s_dma;
}
void *heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (size == CAPTURE_TEXT_SIZE && s_fail_capture) return NULL;
  if (size != sizeof(subghz_rx_burst_t)) return malloc(size);
  return allocate_memory(size);
}
esp_err_t cc1101_init(void) { assert(s_has_resource); return s_failure == FAIL_RADIO_INIT ? ESP_FAIL : ESP_OK; }
esp_err_t cc1101_enter_rx_mode(void) { assert(s_has_resource); return s_failure == FAIL_RX_ENTRY ? ESP_FAIL : ESP_OK; }
esp_err_t cc1101_strobe(uint8_t command) { assert(s_has_resource);
  return ESP_OK;
}
esp_err_t cc1101_set_frequency(uint32_t frequency) { assert(s_has_resource);
  assert(!s_is_enabled && s_stale_events == 0);
  if (s_hop_case && s_hops++ == 0) s_first_hop = s_now;
  return s_failure == FAIL_HOP ? ESP_FAIL : ESP_OK;
}
esp_err_t cc1101_set_preset(cc1101_preset_t preset, uint32_t frequency) {
  assert(s_has_resource);
  if (preset == CC1101_PRESET_IDLE && s_idle_failures != 0) {
    --s_idle_failures;
    return ESP_FAIL;
  }
  return preset != CC1101_PRESET_IDLE && s_failure == FAIL_PRESET ? ESP_FAIL : ESP_OK;
}
static cc1101_radio_config_t s_applied_radio;
esp_err_t cc1101_set_radio_config(const cc1101_radio_config_t *config, uint32_t frequency) {
  assert(cc1101_config_validate(config) == ESP_OK);
  s_applied_radio = *config;
  return cc1101_set_preset(config->preset, frequency);
}

esp_err_t cc1101_get_register(uint8_t reg, uint8_t *out) { *out = 0; return ESP_OK; }
float cc1101_convert_rssi(uint8_t raw) { return s_hop_case == 2 || (s_burst_case == 3 && s_burst_blocks) ? -40 : -110; }

void subghz_tx_stop(void) { ++s_tx_stops; }
esp_err_t resource_acquire(const res_request_t *request, res_handle_t *out) {
  if (s_failure == FAIL_RESOURCE)
    return ESP_ERR_INVALID_STATE;
  assert(!s_has_resource);
  s_has_resource = true;
  *out = 42;
  return ESP_OK;
}
esp_err_t resource_release(res_handle_t handle) {
  assert(s_has_resource && handle == 42 && !s_has_channel);
  assert(!s_has_queue && s_dma == NULL && s_decode == NULL);
  s_has_resource = false;
  return ESP_OK;
}
void led_signal_error(void) {}
void led_signal_info(void) {}
void led_signal_warning(void) {}
bool subghz_protocol_registry_decode_all(const int32_t *pulses, size_t count, subghz_data_t *out) {
  if (!s_burst_case) return false;
  assert(!s_is_enabled && s_progress.phase == SUBGHZ_RX_PHASE_ANALYZING);
  ++s_decodes;
  if (s_burst_case >= 10 && pulses[0] == 33) return false;
  *out = (subghz_data_t){.protocol_name="CAME", .raw_value=pulses[0] == 350 ? 42 : 99,
                        .bit_count=12};
  return true;
}
bool subghz_analyzer_process(const int32_t *pulses, size_t count, subghz_analyzer_result_t *out) { return false; }
int subghz_storage_read(const char *name, char *out, size_t capacity) { return -1; }
size_t subghz_protocol_serialize_raw(const int32_t *pulses, size_t count, uint32_t frequency, char *out, size_t size) {
  strcpy(out, "RAW capture"); return strlen(out);
}
size_t subghz_protocol_serialize_decoded(const subghz_data_t *data, uint32_t frequency, uint32_t te, char *out, size_t size) {
  assert(!s_is_enabled && s_decodes >= 3 && data->raw_value == 42);
  strcpy(out, "decoded capture"); return strlen(out);
}
esp_err_t subghz_storage_save_capture(const char *name, const char *text) {
  assert(!s_has_resource && (strcmp(text, "decoded capture") == 0 || strcmp(text, "RAW capture") == 0));
  ++s_saves; return ESP_OK;
}

static void run(failure_t failure, bool is_cancelled, bool has_cleanup_failure) {
  assert(!subghz_receiver_is_running());
  s_failure = failure;
  s_result_mutex = NULL;
  s_worker = NULL;
  s_inject_stop = is_cancelled;
  s_receive_calls = s_retries = s_queue_events = 0;
  s_expect_dma = has_cleanup_failure;
  s_disable_failures = has_cleanup_failure ? 2 : 0;
  s_delete_failures = has_cleanup_failure ? 2 : 0;
  s_test_hopping = failure == FAIL_HOP || failure == FAIL_RX_ENTRY;
  s_clock_calls = 0;
  s_stale_events = s_test_hopping ? 1 : 0;
  cc1101_radio_config_t radio;
  assert(cc1101_config_from_preset(CC1101_PRESET_2FSK_2KHZ, &radio) == ESP_OK);
  assert(cc1101_config_step(&radio, CC1101_CONFIG_LNA_GAIN, 1) == ESP_OK);
  uint8_t gain = radio.agcctrl2;
  esp_err_t error = subghz_receiver_start_configured(
      SUBGHZ_MODE_SCAN, &radio, s_test_hopping ? 0 : DEFAULT_FREQ);
  radio.agcctrl2 = 0;
  if (failure == FAIL_RESOURCE || failure == FAIL_MUTEX || failure == FAIL_TASK) {
    assert(error != ESP_OK && s_worker == NULL);
  } else {
    assert(error == ESP_OK && s_worker != NULL);
    s_worker(NULL);
    if (s_receive_calls != 0)
      assert(s_applied_radio.agcctrl2 == gain);
  }
  assert(!s_has_resource && !subghz_receiver_is_running());
  assert(!s_has_channel && !s_has_queue && s_dma == NULL && s_decode == NULL);
  if (is_cancelled || failure == FAIL_RADIO_INIT || failure == FAIL_PRESET)
    assert(s_receive_calls == 0);
  if (failure >= FAIL_RADIO_INIT) {
    assert(subghz_receiver_get_error() == ESP_FAIL);
    if (s_test_hopping)
      assert(s_receive_calls == 1 && s_stale_events == 0 && s_rx_freq == DEFAULT_FREQ);
  }
  if (has_cleanup_failure)
    assert(s_retries == 4 && s_queue_events == 2);
  subghz_receiver_stop();
  assert(!subghz_receiver_is_running());
}

int main(void) {
  subghz_receiver_stop();
  assert(!subghz_receiver_is_running());
  assert(subghz_receiver_start(SUBGHZ_MODE_COUNT, 3, DEFAULT_FREQ) == ESP_ERR_INVALID_ARG);
  for (failure_t failure = FAIL_NONE; failure <= FAIL_RX_ENTRY; ++failure)
    run(failure, false, false);
  s_idle_failures = 2; run(FAIL_HOP, false, false); assert(s_retries == 2);
  run(FAIL_NONE, true, false);
  run(FAIL_NONE, false, true);
  run(FAIL_RECEIVE, false, true);
  run(FAIL_NONE, false, false);
  subghz_data_t data={.protocol_name="CAME",.raw_value=42};
  publish_rx_result(true,&data,NULL,NULL,0);
  subghz_rx_result_t first, second;
  assert(subghz_receiver_get_result(&first));
  run(FAIL_NONE,false,false);
  assert(!subghz_receiver_get_result(&second));
  publish_rx_result(true,&data,NULL,NULL,0);
  assert(subghz_receiver_get_result(&second) && first.seq!=second.seq);
  assert(second.data.protocol_name==second.name_buf && strcmp(second.name_buf,"CAME")==0);
  for (s_burst_case = 1; s_burst_case <= 4; ++s_burst_case) {
    s_failure = FAIL_NONE; s_inject_stop = s_expect_dma = s_test_hopping = false;
    s_burst_blocks = s_decodes = s_saves = s_now = 0;
    assert(subghz_receiver_start(SUBGHZ_MODE_SCAN, 3, DEFAULT_FREQ) == ESP_OK);
    s_worker(NULL);
    subghz_rx_result_t capture;
    if (s_burst_case == 4) {
      assert(!s_decodes && !s_saves && !subghz_receiver_get_result(&capture));
    } else {
      assert(s_saves == 0 && subghz_receiver_get_result(&capture));
      assert(subghz_receiver_save(capture.seq + 1, "wrong") == ESP_ERR_INVALID_STATE);
      assert(s_saves == 0);
      assert(subghz_receiver_save(capture.seq, "test") == ESP_OK && s_saves == 1);
      assert(capture.data.raw_value == 42 && capture.matching_frames == s_burst_blocks - 1);
      assert(capture.pulse_count == s_burst_blocks * 32 && capture.frame_count == s_burst_blocks);
      assert(capture.is_limited == (s_burst_case == 2 || s_burst_case == 3));
      assert(s_now >= BURST_QUIET_MS || s_burst_case == 2);
      subghz_rx_progress_t progress; assert(subghz_receiver_get_progress(&progress));
      assert(progress.phase == SUBGHZ_RX_PHASE_COMPLETE && progress.rssi_count > 0);
      if (s_burst_case == 3) assert(s_now >= BURST_MAX_MS);
    }
  }
  subghz_receiver_discard();
  assert(s_capture_text == NULL);
  s_burst_case = 0;
  for (s_hop_case = 1; s_hop_case <= 2; ++s_hop_case) {
    s_now = s_hops = s_first_hop = 0;
    assert(subghz_receiver_start(SUBGHZ_MODE_SCAN, 3, 0) == ESP_OK);
    s_worker(NULL);
    assert(s_hops > 0);
    assert(s_first_hop == (s_hop_case == 1 ? HOP_INTERVAL_MS : HOP_SIGNAL_HOLD_MS));
    assert(!subghz_receiver_get_result(&second));
  }
  s_hop_case = 0;
  for (unsigned raw_case = 1; raw_case <= 9; ++raw_case) {
    s_burst_case = raw_case; s_now = s_burst_blocks = s_decodes = s_saves = 0;
    assert(subghz_receiver_start(SUBGHZ_MODE_RAW, 3, DEFAULT_FREQ) == ESP_OK);
    s_worker(NULL);
    assert(!s_decodes && !s_saves);
    if (raw_case == 4 || raw_case >= 7) {
      assert(!subghz_receiver_get_result(&second));
      if (raw_case == 7) assert(subghz_receiver_get_error() == ESP_ERR_NO_MEM);
    } else {
      assert(subghz_receiver_get_result(&second) && !second.is_limited);
      assert(second.frame_count == 160 && second.pulse_count == 160 * 8192);
      assert(s_now > BURST_MAX_MS);
      subghz_raw_file_t *pending = NULL;
      assert(subghz_receiver_acquire_raw(second.seq, &pending) == ESP_OK);
      assert(subghz_receiver_save(second.seq, "test") == ESP_OK && s_saves == 1);
      subghz_receiver_discard();
      subghz_raw_file_release(pending);
    }
    subghz_receiver_discard();
  }
  s_saves = 0;
  s_burst_case = 1;
  s_now = s_burst_blocks = s_decodes = 0; s_fail_capture = true;
  assert(subghz_receiver_start(SUBGHZ_MODE_SCAN, 3, DEFAULT_FREQ) == ESP_OK);
  s_worker(NULL);
  assert(!subghz_receiver_get_result(&second) && !s_saves && s_capture_text == NULL);
  assert(subghz_receiver_get_error() == ESP_ERR_NO_MEM);
  for (unsigned noise_case = 10; noise_case <= 12; ++noise_case) {
    s_burst_case = noise_case;
    s_failure = FAIL_NONE;
    s_fail_capture = false;
    s_burst_blocks = s_decodes = s_saves = s_now = 0;
    s_stale_events = 1;
    assert(subghz_receiver_start(SUBGHZ_MODE_SCAN, 2, DEFAULT_FREQ) == ESP_OK);
    s_worker(NULL);
    assert(!s_has_resource && !s_has_channel && !s_has_queue);
    subghz_rx_progress_t progress;
    assert(subghz_receiver_get_progress(&progress) && progress.rejected_bursts == 1);
    assert(s_stale_events == 0);
    subghz_rx_result_t capture;
    if (noise_case == 10) {
      assert(subghz_receiver_get_result(&capture) && capture.decoded);
      assert(capture.data.raw_value == 42 && capture.matching_frames == 3);
      assert(capture.pulse_count == 96 && capture.frame_count == 3);
    } else {
      assert(!subghz_receiver_get_result(&capture));
      assert(subghz_receiver_get_error() == (noise_case == 12 ? ESP_FAIL : ESP_OK));
    }
    assert(s_saves == 0);
    subghz_receiver_discard();
  }
  puts("Noise RX: rejected burst followed by valid signal, cancellation and rearm failure passed");
  puts("RAW streaming: >1.3 million pulses, >10s, DMA reuse, manual finish, overflow, SD errors and discard passed");
  puts("Burst RX: no early decode, majority match, silence, limits, live RSSI and cancellation passed");
  puts("Sub-GHz RX lifecycle: radio failures, channel flush, cancellation, contention, allocation and retained DMA/IDLE cleanup passed");
  return 0;
}

struct subghz_raw_file { unsigned refs; size_t count; };
esp_err_t subghz_raw_file_create(uint32_t hz, const cc1101_radio_config_t *radio, subghz_raw_file_t **out) {
  *out = calloc(1, sizeof(**out)); assert(*out); (*out)->refs=1; return ESP_OK;
}
esp_err_t subghz_raw_file_append(subghz_raw_file_t *file, const int32_t *pulses, size_t count) {
  for (size_t i=0;i<count;++i) assert(pulses[i] == ((i&1) ? -700 : 350));
  file->count += count; return s_burst_case == 8 ? ESP_FAIL : ESP_OK;
}
esp_err_t subghz_raw_file_finish(subghz_raw_file_t *file) { assert(file->count); return s_burst_case == 9 ? ESP_FAIL : ESP_OK; }
esp_err_t subghz_raw_file_save(subghz_raw_file_t *file, const char *name) { ++s_saves; return ESP_OK; }
void subghz_raw_file_acquire(subghz_raw_file_t *file) { ++file->refs; }
void subghz_raw_file_release(subghz_raw_file_t *file) { if (file && !--file->refs) free(file); }
