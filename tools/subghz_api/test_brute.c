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

#include "test_platform.h"
void led_signal_info(void);
#include "subghz_brute.c"
static void (*s_entry)(void *);
static unsigned s_sends, s_waits, s_remaining, s_stops, s_mode;
static size_t encode(const subghz_data_t *data, int32_t *out, size_t size) {
  assert(size >= 2 && data->raw_value == s_sends);
  out[0] = 100; out[1] = -100;
  return s_mode == 1 ? 0 : 2;
}
static const subghz_protocol_t s_protocol = {.name = "test", .encode = encode};
const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name) {
  return &s_protocol;
}
subghz_settings_t subghz_settings_get(void) {
  subghz_settings_t settings = {.preset = CC1101_PRESET_OOK_270KHZ};
  assert(cc1101_config_from_preset(settings.preset, &settings.radio) == ESP_OK);
  return settings;
}
void *heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return s_mode == 4 ? NULL : malloc(size);
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t wait) { return pdTRUE; }
void xSemaphoreGive(SemaphoreHandle_t handle) {}
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name, uint32_t stack,
                                 void *context, int priority, TaskHandle_t *handle, int core) {
  assert(core == SYS_CORE_RADIO); s_entry = entry; return pdPASS;
}
void vTaskDelete(TaskHandle_t task) {}
void vTaskDelay(TickType_t ticks) {
  ++s_waits;
  if (s_mode == 3) subghz_brute_stop();
}
bool subghz_receiver_is_running(void) { return false; }
void subghz_receiver_stop(void) {}
esp_err_t subghz_tx_init(void) { return ESP_OK; }
void subghz_tx_stop(void) { ++s_stops; s_remaining = 0; }
esp_err_t subghz_tx_send_configured(const int32_t *pulses, size_t count,
                                    const subghz_tx_config_t *config) {
  assert(s_remaining == 0 && count == 2 && config->frequency_hz == 433920000);
  assert(config->has_radio_config && !config->repeat_until_stopped);
  ++s_sends; s_remaining = 5;
  return ESP_OK;
}
void subghz_tx_get_status(subghz_tx_status_t *status) {
  status->is_busy = s_remaining != 0;
  if (s_remaining) --s_remaining;
  status->error = s_mode == 2 ? ESP_FAIL : ESP_OK;
}
void led_signal_error(void) {}
void led_signal_info(void) {}
int main(void) {
  for (s_mode = 0; s_mode <= 4; ++s_mode) {
    s_sends = s_waits = s_remaining = s_stops = 0;
    assert(subghz_brute_start("test", 2, 433920000) == ESP_OK);
    assert(subghz_brute_start("test", 2, 433920000) == ESP_ERR_INVALID_STATE);
    s_entry(NULL);
    subghz_brute_status_t status;
    assert(subghz_brute_get_status(&status) && status.done && !status.running);
    assert(s_stops == 1 && !atomic_load(&s_active));
    if (s_mode == 0) {
      assert(status.sent == 4 && s_sends == 4 && s_waits >= 20 && status.error == ESP_OK);
    } else {
      assert(status.sent == 0);
      assert(status.error == (s_mode == 1 ? ESP_ERR_INVALID_SIZE :
          s_mode == 2 ? ESP_FAIL : s_mode == 4 ? ESP_ERR_NO_MEM : ESP_OK));
    }
  }
  assert(subghz_brute_start("test", 2, 500000000) == ESP_ERR_INVALID_ARG);
  puts("PASS: sweep completion, failed encoding/TX/allocation, cancellation and reentry");
}
