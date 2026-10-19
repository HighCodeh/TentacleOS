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
#define strlcpy copy_string
#define free release_memory
#define malloc allocate_memory
#include "subghz_receiver.c"
#undef malloc
#undef free

typedef enum {
  FAIL_NONE, FAIL_MUTEX, FAIL_QUEUE, FAIL_TASK, FAIL_CHANNEL, FAIL_CALLBACK,
  FAIL_ENABLE, FAIL_DMA, FAIL_DECODE, FAIL_RECEIVE, FAIL_RESOURCE
} failure_t;
static failure_t s_failure;
static void (*s_worker)(void *);
static unsigned int s_disable_failures, s_delete_failures, s_retries;
static unsigned int s_tx_stops, s_receive_calls, s_queue_events;
static bool s_has_resource, s_has_channel, s_has_queue, s_is_enabled;
static bool s_inject_stop, s_expect_dma;

static void *allocate_memory(size_t size) {
  if (s_failure == FAIL_DECODE)
    return NULL;
  s_decode = malloc(size);
  assert(s_decode != NULL);
  return s_decode;
}

static void release_memory(void *pointer) {
  assert(!s_has_channel && !s_is_enabled);
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
BaseType_t xQueueReceive(QueueHandle_t queue, void *out, TickType_t timeout) {
  assert(s_has_queue && s_has_resource);
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
  assert(ticks == CLEANUP_RETRY_MS && s_has_channel && s_has_resource);
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
TickType_t xTaskGetTickCount(void) { return 0; }
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config, rmt_channel_handle_t *out) {
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
  ++s_receive_calls;
  return s_failure == FAIL_RECEIVE ? ESP_FAIL : ESP_OK;
}
void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned int caps) {
  if (s_failure == FAIL_DMA)
    return NULL;
  s_dma = malloc(size);
  assert(s_dma != NULL);
  return s_dma;
}
void cc1101_strobe(uint8_t command) { assert(s_has_resource); }
void cc1101_set_frequency(uint32_t frequency) { assert(s_has_resource); }
void cc1101_set_preset(cc1101_preset_t preset, uint32_t frequency) { assert(s_has_resource); }
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
void led_signal_info(void) {}
void led_signal_warning(void) {}
bool subghz_protocol_registry_decode_all(const int32_t *pulses, size_t count, subghz_data_t *out) { return false; }
bool subghz_analyzer_process(const int32_t *pulses, size_t count, subghz_analyzer_result_t *out) { return false; }
esp_err_t subghz_storage_save_raw(const char *name, const int32_t *pulses, size_t count, uint32_t frequency) { return ESP_OK; }
esp_err_t subghz_storage_save_decoded(const char *name, const subghz_data_t *data, uint32_t frequency, uint32_t te) { return ESP_OK; }

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
  esp_err_t error = subghz_receiver_start(SUBGHZ_MODE_SCAN, 3, DEFAULT_FREQ);
  if (failure == FAIL_RESOURCE || failure == FAIL_MUTEX || failure == FAIL_TASK) {
    assert(error != ESP_OK && s_worker == NULL);
  } else {
    assert(error == ESP_OK && s_worker != NULL);
    s_worker(NULL);
  }
  assert(!s_has_resource && !subghz_receiver_is_running());
  assert(!s_has_channel && !s_has_queue && s_dma == NULL && s_decode == NULL);
  if (is_cancelled)
    assert(s_receive_calls == 0);
  if (has_cleanup_failure)
    assert(s_retries == 4 && s_queue_events == 2);
  subghz_receiver_stop();
  assert(!subghz_receiver_is_running());
}

int main(void) {
  subghz_receiver_stop();
  assert(!subghz_receiver_is_running());
  assert(subghz_receiver_start(SUBGHZ_MODE_COUNT, 3, DEFAULT_FREQ) == ESP_ERR_INVALID_ARG);
  for (failure_t failure = FAIL_NONE; failure <= FAIL_RESOURCE; ++failure)
    run(failure, false, false);
  run(FAIL_NONE, true, false);
  run(FAIL_NONE, false, true);
  run(FAIL_RECEIVE, false, true);
  run(FAIL_NONE, false, false);
  subghz_data_t data={.protocol_name="CAME",.raw_value=42};
  publish_rx_result(true,&data,NULL,NULL);
  subghz_rx_result_t first, second;
  assert(subghz_receiver_get_result(&first));
  run(FAIL_NONE,false,false);
  assert(!subghz_receiver_get_result(&second));
  publish_rx_result(true,&data,NULL,NULL);
  assert(subghz_receiver_get_result(&second) && first.seq!=second.seq);
  assert(second.data.protocol_name==second.name_buf && strcmp(second.name_buf,"CAME")==0);
  puts("Sub-GHz RX lifecycle: cancellation, contention, allocation and retained DMA cleanup passed");
  return 0;
}
