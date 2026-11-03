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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"
#include "ir_ac.h"
#include "ir_lease.h"
#include "ir_remote.h"
#include "spi_bridge.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "resource_mgr.h"

int critical, is_isr, task_fail;
static int64_t s_now;
static uintptr_t s_caller = 1;
static unsigned s_transmits;
static bool s_fail_disable, s_fail_delete, s_fail_encoder, s_drop_commit, s_offline;
static bool s_tx_pending, s_rx_pending, s_torch;
static uint8_t s_drop_action = UINT8_MAX;
static bool s_reset_during_tx;
static const rmt_symbol_word_t *s_tx_buffer;
static size_t s_tx_count;
static rmt_symbol_word_t s_optical[4096];
static size_t s_optical_count;
static rmt_symbol_word_t *s_rx_buffer;
static size_t s_rx_capacity;
static rmt_rx_event_callbacks_t s_callbacks;
static bool s_receive_frame;
static res_handle_t s_resource;
static uint64_t s_wire_epoch;
static uint32_t s_wire_operation;
static uint16_t s_wire_count;
void test_c5_tick(void);

int64_t esp_timer_get_time(void) { return s_now; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "mock"; }
void esp_fill_random(void *p, size_t n) { static uint64_t nonce = 42; assert(n == sizeof(nonce)); ++nonce; memcpy(p, &nonce, n); }
void *xTaskGetCurrentTaskHandle(void) { return (void *)s_caller; }
esp_err_t resource_acquire(const res_request_t *request, res_handle_t *out) {
  assert(!request->allow_preempt);
  if (s_resource) return ESP_ERR_INVALID_STATE;
  *out = s_resource = 1;
  return ESP_OK;
}
esp_err_t resource_release(res_handle_t handle) { assert(handle == s_resource); s_resource = 0; return ESP_OK; }
esp_err_t gpio_set_level(int pin, int level) { assert(pin == 6); s_torch = level; return ESP_OK; }
esp_err_t gpio_set_direction(int pin, int mode) { assert(pin == 6 && mode == 1); return ESP_OK; }
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, void **out) {
  assert(config->gpio_num == 6 && config->mem_block_symbols == 48 && config->trans_queue_depth == 1);
  *out = (void *)1; return ESP_OK;
}
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config, void **out) {
  assert(config->gpio_num == 10 && config->mem_block_symbols == 48 && config->flags.invert_in); *out = (void *)2; return ESP_OK;
}
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config, void **out) { assert(config); *out = (void *)3; return ESP_OK; }
esp_err_t rmt_del_encoder(void *encoder) { assert(encoder); return s_fail_encoder ? ESP_FAIL : ESP_OK; }
esp_err_t rmt_enable(void *channel) { assert(channel); return ESP_OK; }
esp_err_t rmt_disable(void *channel) {
  assert(channel);
  if (s_fail_disable) return ESP_FAIL;
  if (channel == (void *)1) { s_tx_pending = false; s_tx_buffer = NULL; }
  else { s_rx_pending = false; s_rx_buffer = NULL; }
  return ESP_OK;
}
esp_err_t rmt_del_channel(void *channel) { assert(channel); return s_fail_delete ? ESP_FAIL : ESP_OK; }
esp_err_t rmt_apply_carrier(void *channel, const rmt_carrier_config_t *carrier) {
  assert(channel); if (carrier) assert(carrier->frequency_hz >= 10000 && carrier->frequency_hz <= 100000);
  return ESP_OK;
}
esp_err_t rmt_transmit(void *channel, void *encoder, const void *data, size_t size,
                        const rmt_transmit_config_t *config) {
  assert(channel && encoder && !config->loop_count && !config->flags.eot_level);
  s_tx_count = size / sizeof(rmt_symbol_word_t);
  assert(s_tx_count <= 4096);
  s_tx_buffer = data;
  memcpy(s_optical, data, size);
  s_optical_count = s_tx_count;
  s_transmits++;
  s_tx_pending = true;
  return ESP_OK;
}
esp_err_t rmt_tx_wait_all_done(void *channel, int ticks) {
  assert(channel && ticks == 0);
  if (s_tx_pending) {
    assert(s_tx_buffer[0].duration0 != 0);
    if (s_fail_disable || s_reset_during_tx) return ESP_ERR_TIMEOUT;
    s_tx_pending = false;
  }
  return ESP_OK;
}
esp_err_t rmt_rx_register_event_callbacks(void *channel, const rmt_rx_event_callbacks_t *cb, void *context) {
  assert(channel && context == NULL); s_callbacks = *cb; return ESP_OK;
}
esp_err_t rmt_receive(void *channel, void *buffer, size_t capacity, const rmt_receive_config_t *config) {
  assert(channel && config->signal_range_max_ns == 12000000);
  s_rx_buffer = buffer; s_rx_capacity = capacity / sizeof(rmt_symbol_word_t); s_rx_pending = true;
  return ESP_OK;
}
void vTaskDelay(unsigned ticks) {
  assert(!critical);
  s_now += ticks * 1000;
  if (s_reset_during_tx && s_tx_pending) { ir_remote_disconnect(); s_reset_during_tx = false; }
  if (s_receive_frame && s_rx_pending) {
    size_t n = s_optical_count < s_rx_capacity ? s_optical_count : s_rx_capacity;
    memcpy(s_rx_buffer, s_optical, n * sizeof(*s_rx_buffer));
    rmt_rx_done_event_data_t event = {.received_symbols = s_rx_buffer, .num_symbols = n};
    s_callbacks.on_recv_done((void *)2, &event, NULL);
    s_rx_pending = false;
  }
  test_c5_tick();
}

esp_err_t spi_bridge_send_command(spi_id_t id, const void *payload, uint16_t length,
                                  spi_header_t *header, void *out, size_t capacity, uint32_t timeout) {
  assert(!critical && id == SPI_ID_IR_REMOTE && timeout == 250);
  spi_ir_request_t request;
  memcpy(&request, payload, sizeof(request));
  if (s_offline) { vTaskDelay(timeout); return ESP_ERR_TIMEOUT; }
  uint8_t data[255], size;
  spi_status_t status = ir_remote_dispatch(payload, length, data, &size);
  if (status != SPI_STATUS_OK) return status == SPI_STATUS_BUSY ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  assert(size <= capacity);
  memcpy(out, data, size); header->length = size;
  spi_ir_reply_t reply; memcpy(&reply, data, sizeof(reply));
  s_wire_epoch = reply.epoch; s_wire_operation = reply.operation; s_wire_count = reply.count;
  if ((s_drop_commit && request.action == SPI_IR_COMMIT) || request.action == s_drop_action) {
    s_drop_commit = false; s_drop_action = UINT8_MAX; return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

static spi_ir_reply_t rpc(spi_ir_request_t request, const uint8_t *chunk, spi_status_t expected) {
  uint8_t data[255], response[255], length = sizeof(request), written;
  memcpy(data, &request, length);
  if (request.action == SPI_IR_UPLOAD) { memcpy(data + length, chunk, request.count * 4); length += request.count * 4; }
  assert(ir_remote_dispatch(data, length, response, &written) == expected);
  spi_ir_reply_t reply = {0};
  if (expected == SPI_STATUS_OK) { assert(written >= sizeof(reply)); memcpy(&reply, response, sizeof(reply)); }
  return reply;
}
static spi_ir_request_t req(uint8_t action, uint64_t epoch, uint32_t op) {
  return (spi_ir_request_t){.epoch = epoch, .operation = op, .action = action, .revision = SPI_IR_REVISION};
}

int main(void) {
  assert(ir_remote_init() == ESP_OK);
  assert(ir_tx_init() == ESP_OK);
  s_caller = 2; assert(ir_rx_init() == ESP_ERR_INVALID_STATE); assert(ir_close() == ESP_ERR_INVALID_STATE);
  s_caller = 1;
  ir_data_t signal = {.protocol = IR_PROTO_NEC, .address = 0x12, .command = 0x34};
  s_drop_commit = true;
  assert(ir_send(&signal) == ESP_OK && s_transmits == 1);
  spi_ir_request_t duplicate = req(SPI_IR_COMMIT, s_wire_epoch, s_wire_operation);
  rpc(duplicate, NULL, SPI_STATUS_OK); vTaskDelay(20); assert(s_transmits == 1);
  assert(ir_close() == ESP_OK && !s_resource);
  puts("PASS: paired TX, lost commit response, duplicate commit and foreign ownership");

  assert(ir_rx_init() == ESP_OK); s_receive_frame = true;
  ir_data_t decoded;
  assert(ir_receive(&decoded, 500) == ESP_OK);
  assert(decoded.protocol == signal.protocol && decoded.address == signal.address && decoded.command == signal.command);
  rmt_symbol_word_t raw[512]; size_t raw_count;
  assert(ir_get_last_raw(raw, 1, &raw_count) == ESP_ERR_INVALID_SIZE && raw_count == 0);
  assert(ir_get_last_raw(raw, 512, &raw_count) == ESP_OK && raw_count == s_optical_count);
  s_receive_frame = false; assert(ir_receive(&decoded, 100) == ESP_ERR_TIMEOUT);
  s_optical_count = 513; s_receive_frame = true;
  assert(ir_receive(&decoded, 100) == ESP_ERR_INVALID_SIZE);
  assert(ir_get_last_raw(raw, 512, &raw_count) == ESP_OK && raw_count == 0);
  assert(ir_close() == ESP_OK); s_receive_frame = false;
  puts("PASS: decoded RX, fragmented fetch, timeout and explicit overflow");

  rmt_symbol_word_t long_frame[4096];
  for (unsigned i = 0; i < 4096; i++) long_frame[i] = (rmt_symbol_word_t){.duration0 = 50, .level0 = 1, .duration1 = 50};
  assert(ir_tx_init() == ESP_OK);
  assert(ir_send_raw(long_frame, 4096, 38000) == ESP_OK && s_optical_count == 4096);
  assert(memcmp(long_frame, s_optical, sizeof(long_frame)) == 0);
  assert(ir_send_raw(long_frame, 4097, 38000) == ESP_ERR_INVALID_ARG);
  long_frame[0].duration0 = 0; assert(ir_send_raw(long_frame, 1, 38000) == ESP_ERR_INVALID_ARG); long_frame[0].duration0 = 50;
  ir_ac_state_t ac = {.protocol = IR_AC_PROTO_HAIER, .power = true, .mode = IR_AC_MODE_COOL, .temp_c = 24, .fan = IR_AC_FAN_AUTO};
  assert(ir_ac_send(&ac) == ESP_OK);
  assert(ir_close() == ESP_OK);
  puts("PASS: full 4096-symbol upload, bounds and AC encoder through remote RMT");

  assert(ir_tx_init() == ESP_OK); s_fail_disable = true;
  assert(ir_send_raw(long_frame, 1, 38000) == ESP_ERR_TIMEOUT);
  assert(ir_close() != ESP_OK && s_resource);
  assert(s_tx_buffer && s_tx_buffer[0].duration0 == 50);
  s_caller = 2; assert(ir_lease_acquire() == ESP_ERR_INVALID_STATE); s_caller = 1;
  s_fail_disable = false; assert(ir_close() == ESP_OK);
  assert(ir_tx_init() == ESP_OK); s_fail_delete = true;
  assert(ir_send_raw(long_frame, 1, 38000) == ESP_ERR_TIMEOUT);
  assert(ir_close() != ESP_OK); s_fail_delete = false; assert(ir_close() == ESP_OK);
  assert(ir_tx_init() == ESP_OK); s_fail_encoder = true;
  assert(ir_send_raw(long_frame, 1, 38000) == ESP_ERR_TIMEOUT);
  assert(ir_close() != ESP_OK); s_fail_encoder = false; assert(ir_close() == ESP_OK);
  puts("PASS: failed disable/delete/encoder teardown retains ownership and live buffers");

  assert(ir_tx_init() == ESP_OK); s_reset_during_tx = true;
  assert(ir_send_raw(long_frame, 1, 38000) == ESP_ERR_INVALID_STATE);
  assert(ir_close() == ESP_OK);
  assert(ir_flash_on() == ESP_OK && s_torch);
  s_offline = true; vTaskDelay(1100); assert(!s_torch);
  assert(ir_flash_off() != ESP_OK && s_resource);
  s_offline = false; assert(ir_flash_off() == ESP_OK && !s_resource);
  puts("PASS: session loss cancels TX, torch hard deadline and offline reconciliation");

  spi_ir_reply_t reply = rpc(req(SPI_IR_OPEN, 900, 0), NULL, SPI_STATUS_OK);
  uint64_t epoch = reply.epoch;
  spi_ir_request_t begin = req(SPI_IR_BEGIN, epoch, 1); begin.count = 2; begin.value = 38000;
  rpc(begin, NULL, SPI_STATUS_OK);
  rpc(req(SPI_IR_COMMIT, epoch, 1), NULL, SPI_STATUS_INVALID_ARG);
  uint8_t chunk[] = {50, 128, 50, 0};
  spi_ir_request_t upload = req(SPI_IR_UPLOAD, epoch, 1); upload.count = 1; upload.offset = 1;
  rpc(upload, chunk, SPI_STATUS_INVALID_ARG); upload.offset = 0;
  rpc(upload, chunk, SPI_STATUS_OK); rpc(upload, chunk, SPI_STATUS_OK);
  chunk[0] = 51; rpc(upload, chunk, SPI_STATUS_INVALID_ARG); chunk[0] = 50;
  rpc(req(SPI_IR_COMMIT, epoch, 1), NULL, SPI_STATUS_INVALID_ARG);
  upload.offset = 1; rpc(upload, chunk, SPI_STATUS_OK);
  unsigned before = s_transmits;
  rpc(req(SPI_IR_CANCEL, epoch, 1), NULL, SPI_STATUS_OK); vTaskDelay(20);
  rpc(req(SPI_IR_COMMIT, epoch, 1), NULL, SPI_STATUS_OK); vTaskDelay(20);
  assert(s_transmits == before);
  begin.operation = 2; rpc(begin, NULL, SPI_STATUS_OK);
  vTaskDelay(2100);
  reply = rpc(req(SPI_IR_STATUS, epoch, 2), NULL, SPI_STATUS_OK); assert(reply.state == SPI_IR_CLOSED);
  rpc(req(SPI_IR_COMMIT, epoch, 2), NULL, SPI_STATUS_BUSY);
  reply = rpc(req(SPI_IR_OPEN, 901, 0), NULL, SPI_STATUS_OK);
  uint64_t newer_epoch = reply.epoch;
  assert(newer_epoch != epoch);
  rpc(req(SPI_IR_COMMIT, epoch, 2), NULL, SPI_STATUS_INVALID_ARG);
  begin.epoch = newer_epoch; begin.operation = 1;
  rpc(begin, NULL, SPI_STATUS_OK);
  begin.count = 1; rpc(begin, NULL, SPI_STATUS_INVALID_ARG); begin.count = 2;
  rpc(req(SPI_IR_CANCEL, newer_epoch, 1), NULL, SPI_STATUS_OK); vTaskDelay(20);
  begin.operation = 2; rpc(begin, NULL, SPI_STATUS_OK);
  rpc(req(SPI_IR_COMMIT, newer_epoch, 1), NULL, SPI_STATUS_INVALID_ARG);
  spi_ir_request_t invalid = req(SPI_IR_STATUS, newer_epoch, 2); invalid.reserved = 1;
  rpc(invalid, NULL, SPI_STATUS_INVALID_ARG);
  invalid.reserved = 0; invalid.revision++;
  rpc(invalid, NULL, SPI_STATUS_UNSUPPORTED);
  ir_remote_disconnect(); vTaskDelay(20);
  reply = rpc(req(SPI_IR_STATUS, newer_epoch, 2), NULL, SPI_STATUS_OK);
  assert(reply.epoch != newer_epoch && reply.state == SPI_IR_CLOSED);
  puts("PASS: missing/out-of-order/changed fragments, cancellation tombstone, incomplete upload expiry");
  return 0;
}
