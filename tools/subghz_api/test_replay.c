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
#include "subghz_replay.c"
static const char *s_content;
static unsigned s_allocations, s_fail_allocation, s_reads, s_sends, s_inits;
void *heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return ++s_allocations == s_fail_allocation ? NULL : malloc(size);
}
int subghz_storage_read(const char *name, char *out, size_t size) {
  assert(strcmp(name, "test") == 0); ++s_reads;
  size_t length = strlen(s_content); assert(length < size);
  memcpy(out, s_content, length + 1); return (int)length;
}
static unsigned s_rx_ticks, s_stop_requests;
bool subghz_receiver_is_running(void) { return s_rx_ticks != 0; }
void subghz_receiver_stop(void) { ++s_stop_requests; }
void vTaskDelay(TickType_t ticks) { assert(ticks == REPLAY_RX_POLL_MS && s_rx_ticks); --s_rx_ticks; }
esp_err_t subghz_tx_init(void) { ++s_inits; return ESP_OK; }
esp_err_t subghz_tx_send_configured(const int32_t *pulses, size_t count, const subghz_tx_config_t *config) {
  assert(count == (strstr(s_content, "Protocol: RAW") ? 3 : 15) && pulses[0] == 350 && pulses[1] == -700);
  assert(config->frequency_hz == 315000000 && config->has_radio_config); ++s_sends;
  return ESP_OK;
}
esp_err_t cc1101_get_radio_config(cc1101_radio_config_t *out) {
  return cc1101_config_from_preset(CC1101_PRESET_OOK_270KHZ, out);
}
uint8_t cc1101_get_active_preset_id(void) { return CC1101_PRESET_OOK_270KHZ; }
static size_t encode_capture(const subghz_data_t *data, int32_t *out, size_t capacity) {
  assert(data->raw_value == 42 && capacity >= 3);
  out[0] = 350; out[1] = -700; out[2] = 350; return 3;
}
const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name) {
  static const subghz_protocol_t encoder = {.encode = encode_capture};
  return strcmp(name, "CAME") == 0 ? &encoder : NULL;
}
size_t subghz_protocol_registry_encode(const char *name, const subghz_data_t *data,
                                      int32_t *out, size_t capacity) {
  return encode_capture(data, out, capacity);
}
/* Codec details are exercised by test_subghz.py; this fixture tests ownership. */
esp_err_t flipper_subghz_encode(const char *fields, int32_t *out, size_t capacity, size_t *out_count) {
  assert(strstr(fields, "Key: 00 00 00 00 00 00 00 2A") != NULL);
  subghz_data_t data = {.raw_value = 42, .bit_count = 12};
  *out_count = encode_capture(&data, out, capacity);
  return ESP_OK;
}

esp_err_t flipper_subghz_describe(const char *fields, char *out, size_t capacity) {
  (void)fields; (void)out; (void)capacity;
  return ESP_ERR_NOT_SUPPORTED;
}

int main(void) {
  s_content = "Frequency: 315000000\nPreset: 1\nProtocol: CAME\nBit: 12\nKey: 00 00 00 00 00 00 00 2A\n";
  subghz_replay_t replay = {0};
  for (unsigned fail = 1; fail <= 2; ++fail) {
    s_allocations = 0; s_fail_allocation = fail;
    assert(subghz_replay_load("test", &replay) == ESP_ERR_NO_MEM);
    assert(replay.pulses == NULL && !s_sends && !s_inits);
    subghz_replay_free(&replay);
  }
  s_fail_allocation = 0;
  assert(subghz_replay_load("test", &replay) == ESP_OK);
  assert(!strcmp(replay.protocol, "CAME") && !strcmp(replay.key, "00 00 00 00 00 00 00 2A"));
  assert(!s_sends && !s_inits);
  unsigned reads = s_reads;
  for (unsigned i = 0; i < 8; ++i) assert(subghz_replay_send(&replay) == ESP_OK);
  assert(s_reads == reads && s_sends == 8);
  subghz_replay_free(&replay); subghz_replay_free(&replay);
  assert(subghz_replay_send(&replay) == ESP_ERR_INVALID_ARG);
  s_content = "Frequency: 315000000\nPreset: 1\nProtocol: RAW\nRAW_Data: 350 -700 350\n";
  assert(subghz_replay_load("test", &replay) == ESP_OK);
  assert(!strcmp(replay.protocol, "RAW") && !replay.key[0]);
  assert(subghz_replay_send(&replay) == ESP_OK); subghz_replay_free(&replay);
  assert(subghz_replay_file("test") == ESP_OK);
  reads = s_reads;
  assert(subghz_replay_prepare(s_content, &replay) == ESP_OK);
  assert(s_reads == reads && !strcmp(replay.protocol, "RAW"));
  s_rx_ticks = 3;
  assert(subghz_replay_send(&replay) == ESP_OK && s_rx_ticks == 0 && s_stop_requests == 1);
  unsigned inits = s_inits, sends = s_sends;
  s_rx_ticks = REPLAY_RX_DRAIN_MS / REPLAY_RX_POLL_MS + 1;
  assert(subghz_replay_send(&replay) == ESP_ERR_TIMEOUT);
  assert(s_inits == inits && s_sends == sends);
  s_rx_ticks = 0;
  subghz_replay_free(&replay);
  s_content = "Protocol: Unsupported\n";
  assert(subghz_replay_load("test", &replay) == ESP_ERR_NOT_SUPPORTED);
  assert(!replay.pulses);
  puts("PASS: prepared replay allocation failures, decoded/RAW metadata, no TX on load, repeated sends without rereads and idempotent cleanup");
}

// File-backed replay is exercised by the real-storage suite; this fixture covers text preparation.
esp_err_t subghz_raw_file_load(const char *name, subghz_raw_file_t **out) { *out=NULL; return ESP_ERR_NOT_SUPPORTED; }
void subghz_raw_file_acquire(subghz_raw_file_t *file) { assert(!file); }
void subghz_raw_file_release(subghz_raw_file_t *file) { assert(!file); }
void subghz_raw_file_info(const subghz_raw_file_t *file, uint32_t *hz, cc1101_radio_config_t *radio, uint64_t *count) { assert(false); }
esp_err_t subghz_tx_send_file(subghz_raw_file_t *file, const subghz_tx_config_t *config) { assert(false); return ESP_FAIL; }
