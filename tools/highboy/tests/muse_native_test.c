#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spi_protocol.h"

typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef unsigned nvs_handle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define SYS_CORE_UI 1
#define SYS_CORE_RADIO 0
#define SYS_PRIO_SERVICE_HI 5
#define SYS_PRIO_SERVICE_LO 4
#define NVS_READWRITE 1
#define ESP_LOGE(tag, ...) ((void)(tag), s_error_logs++)

static int s_error_logs, s_allocation_failures;
static unsigned s_task_creates, s_mic_starts, s_mic_stops, s_spi_calls;
static esp_err_t s_spi_error;
static uint8_t s_spi_reply_length;
static bool s_allow_task;
static uint64_t s_nonce;

static int64_t esp_timer_get_time(void) {return 0;}
static void *heap_caps_calloc(size_t count, size_t bytes, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) ||
         caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (s_allocation_failures > 0) {s_allocation_failures--; return NULL;}
  return calloc(count, bytes);
}
static esp_err_t audio_i2s_mic_stream_start(uint32_t rate) {
  assert(rate == 16000); s_mic_starts++; return ESP_OK;
}
static int audio_i2s_mic_stream_read(int16_t *samples, int count) {
  (void)samples; (void)count; assert(false); return 0;
}
static void audio_i2s_mic_stream_stop(void) {s_mic_stops++;}
static void vTaskDelay(unsigned ticks) {(void)ticks; assert(false);}
static void vTaskDelete(void *handle) {(void)handle; assert(false);}
static int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, uint32_t stack,
                                  void *arg, unsigned priority, void *handle, unsigned core) {
  assert(task != NULL && arg != NULL && handle == NULL);
  if (strcmp(name, "muse_mic") == 0) {
    assert(stack == 3072 && priority == SYS_PRIO_SERVICE_HI && core == SYS_CORE_UI);
  } else {
    assert(strcmp(name, "muse_p4") == 0 && stack == 8192);
    assert(priority == SYS_PRIO_SERVICE_LO && core == SYS_CORE_RADIO);
  }
  s_task_creates++;
  return s_allow_task ? pdPASS : 0;
}
static esp_err_t esp_register_shutdown_handler(void (*handler)(void)) {
  assert(handler != NULL); return ESP_OK;
}
static esp_err_t power_manager_no_sleep_acquire(void) {assert(false); return ESP_OK;}
static esp_err_t power_manager_no_sleep_release(void) {assert(false); return ESP_OK;}
static esp_err_t nvs_open(const char *space, unsigned mode, nvs_handle_t *out_handle) {
  assert(strcmp(space, "muse_p4") == 0 && mode == NVS_READWRITE);
  *out_handle = 1; return ESP_OK;
}
static esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *out_value) {
  assert(handle == 1 && strcmp(key, "nonce") == 0); *out_value = s_nonce; return ESP_OK;
}
static esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value) {
  assert(handle == 1 && strcmp(key, "nonce") == 0); s_nonce = value; return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t handle) {assert(handle == 1); return ESP_OK;}
static void nvs_close(nvs_handle_t handle) {assert(handle == 1);}
static esp_err_t spi_bridge_send_command(spi_id_t command, const void *input, uint8_t bytes,
                                         spi_header_t *out_header, void *out_data,
                                         size_t capacity, uint32_t timeout) {
  assert(command == (spi_id_t)0x1105 && bytes == 2 && input != NULL && timeout == 80);
  assert(((const uint8_t *)input)[0] == 0x12 && capacity >= s_spi_reply_length);
  memset(out_data, 0x34, s_spi_reply_length);
  out_header->length = s_spi_reply_length; s_spi_calls++; return s_spi_error;
}

#include "../../../firmware_p4/components/Service/muse/hb_muse_p4_service.c"

int main(void) {
  hb_muse_p4_owner_t owner = {0};
  const uint8_t payload[] = {0x12, 0x56};
  size_t received = 123;
  assert(!rpc(&owner, RPC_TX, (hb_muse_p4_rpc_input_t){payload, FRAME_MAX + 1}, &received));
  assert(received == 123 && s_spi_calls == 0);
  s_spi_reply_length = 2;
  assert(rpc(&owner, RPC_TX, (hb_muse_p4_rpc_input_t){payload, sizeof payload}, &received));
  assert(received == 2 && owner.response[0] == 0x34 && s_spi_calls == 1);
  assert(RPC_PULL_REPLAY == 0x110c);
  s_spi_error = ESP_ERR_INVALID_STATE;
  assert(!rpc(&owner, RPC_TX, (hb_muse_p4_rpc_input_t){payload, sizeof payload}, NULL));

  s_requested = s_held = true;
  s_generation = owner.generation = 1;
  s_allocation_failures = 1;
  assert(!mic_start(&owner, 16000));
  assert(s_error_logs == 1 && owner.recording_pcm == NULL && s_mic_starts == 0);
  assert(!mic_start(&owner, 8000) && s_mic_starts == 0);
  assert(!mic_start(&owner, 16000));
  assert(!owner.recorder_live && !owner.recording && !owner.mic);
  assert(s_mic_starts == 1 && s_mic_stops == 1 && s_task_creates == 1);
  free(owner.recording_pcm);
  s_requested = s_held = false;

  s_allocation_failures = 2;
  unsigned creates = s_task_creates;
  assert(hb_muse_p4_start(HB_MUSE_P4_CHAT) == ESP_ERR_NO_MEM);
  assert(!hb_muse_p4_is_active() && s_task_creates == creates);
  s_allocation_failures = 1; // PSRAM failure followed by successful internal fallback.
  assert(hb_muse_p4_start(HB_MUSE_P4_CHAT) == ESP_ERR_NO_MEM);
  assert(!hb_muse_p4_is_active() && s_task_creates == creates + 1);
  assert(s_error_logs == 6);
  assert(hb_muse_p4_start(HB_MUSE_P4_OFF) == ESP_ERR_INVALID_ARG);
  puts("PASS: native Muse RPC bounds/wire values, allocation failures, task rollback and core policy");
  return 0;
}
