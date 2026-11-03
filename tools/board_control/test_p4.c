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
#include "board_service.c"

int test_c5_init(void);
int test_c5_rpc(spi_id_t id, const void *data, uint16_t len, void *out, uint8_t *out_len);
void test_c5_expire(void);
void test_c5_reboot(void);

int critical, is_isr, task_fail;
static int64_t now = 1000;
static uint64_t random_value = 100;
static uint8_t physical = SPI_BOARD_SAFE_VALUES;
static uint8_t fail_gpio;
static unsigned writes, set_calls, rpc_calls;
static int fault;
static bool input_high;

int64_t esp_timer_get_time(void) { return now; }
void esp_fill_random(void *p, size_t n) { assert(n == sizeof(random_value)); ++random_value; memcpy(p, &random_value, n); }
static int gpio_apply(unsigned id, bool value) {
  writes++;
  if (fail_gpio & (1U << id)) return ESP_FAIL;
  physical = (physical & ~(1U << id)) | (value ? 1U << id : 0);
  return ESP_OK;
}
esp_err_t board_pins_set_amplifier(bool v) { return gpio_apply(SPI_BOARD_AMP, v); }
esp_err_t board_pins_set_haptic_release(bool v) { return gpio_apply(SPI_BOARD_HAPTIC, v); }
esp_err_t board_pins_set_usb_enable(bool v) { return gpio_apply(SPI_BOARD_USB, v); }
bool board_pins_haptic_int_high(void) { return input_high; }
bool board_pins_haptic_irq_pending(void) { return true; }

esp_err_t spi_bridge_send_command(spi_id_t id, const void *data, uint16_t len,
spi_header_t *header, void *out, size_t capacity, uint32_t timeout) {
  assert(!critical && timeout == BOARD_RPC_MS);
  rpc_calls++;
  if (id == SPI_ID_BOARD_SET) {
    set_calls++;
    if (fault == 2) { fault = 0; return ESP_ERR_TIMEOUT; }
    if (fault == 3) { fault = 0; test_c5_reboot(); }
  }
  uint8_t bytes[sizeof(spi_board_caps_t) + 1] = {0}, length = 0;
  int result = test_c5_rpc(id, data, len, bytes, &length);
  if (id == SPI_ID_BOARD_SET && fault == 1) { fault = 0; return ESP_ERR_TIMEOUT; }
  if (id == SPI_ID_BOARD_NEGOTIATE) {
    spi_board_caps_t caps;
    memcpy(&caps, bytes, sizeof(caps));
    if (fault == 4) caps.revision++;
    if (fault == 5) caps.capabilities &= ~SPI_BOARD_CAP_AMP;
    memcpy(bytes, &caps, sizeof(caps));
  }
  if (fault == 6) length++;
  if (fault == 7) length--;
  header->length = length < capacity ? length : capacity;
  memcpy(out, bytes, header->length);
  switch (result) {
    case SPI_STATUS_OK: return ESP_OK;
    case SPI_STATUS_BUSY: return ESP_ERR_INVALID_STATE;
    case SPI_STATUS_UNSUPPORTED: return ESP_ERR_NOT_SUPPORTED;
    case SPI_STATUS_INVALID_ARG: return ESP_ERR_INVALID_ARG;
    default: return ESP_FAIL;
  }
}

static board_control_status_t snapshot(void) {
  board_control_status_t status;
  assert(get_status(&status) == ESP_OK);
  return status;
}
static void reconnect(void) { invalidate(ESP_ERR_TIMEOUT); fault = 0; poll_once(); assert(snapshot().is_compatible); }

int main(void) {
  _Static_assert(sizeof(spi_board_query_t) == 8, "query wire");
  assert(request_amplifier(true) == ESP_ERR_INVALID_STATE);
  assert(get_status(NULL) == ESP_ERR_INVALID_ARG);
  task_fail = 1;
  assert(board_service_init() == ESP_ERR_NO_MEM);
  task_fail = 0;
  assert(board_service_init() == ESP_OK);
  assert(test_c5_init() == ESP_OK);
  poll_once();
  assert(snapshot().is_compatible && physical == SPI_BOARD_SAFE_VALUES);
  assert(!snapshot().outputs[BOARD_CONTROL_USB].has_request);
  unsigned before = rpc_calls;
  assert(request_amplifier(true) == ESP_OK);
  assert(request_amplifier(false) == ESP_ERR_INVALID_STATE);
  assert(rpc_calls == before && snapshot().outputs[0].is_pending);
  assert(request_haptic(true) == ESP_OK);
  poll_once();
  assert(physical == SPI_BOARD_OUTPUT_MASK);
  assert(snapshot().outputs[0].confirmed && !snapshot().outputs[0].is_pending);
  before = writes;
  assert(request_amplifier(true) == ESP_OK);
  poll_once();
  assert(writes == before);
  assert(request_usb(false) == ESP_OK);
  poll_once();
  assert(physical == (SPI_BOARD_CAP_AMP | SPI_BOARD_CAP_HAPTIC));
  input_high = true;
  poll_once();
  assert(snapshot().is_input_known && snapshot().is_haptic_int_high);
  assert(snapshot().is_haptic_irq_pending);
  is_isr = 1;
  assert(request_usb(true) == ESP_ERR_INVALID_STATE);
  is_isr = 0;

  // A lost ACK leaves the result unknown; query reconciles without resending SET.
  assert(request_amplifier(false) == ESP_OK);
  fault = 1;
  poll_once();
  assert(!(physical & SPI_BOARD_CAP_AMP));
  assert(!snapshot().is_compatible && !snapshot().outputs[0].is_known);
  assert(snapshot().outputs[0].result == ESP_ERR_TIMEOUT);
  before = set_calls;
  poll_once();
  assert(snapshot().is_compatible && !snapshot().outputs[0].confirmed);
  assert(set_calls == before);
  assert(request_amplifier(true) == ESP_OK);
  fault = 2;
  poll_once();
  poll_once();
  assert(!snapshot().outputs[0].confirmed);

  // Reboot between read and write rejects the previous epoch.
  assert(request_amplifier(true) == ESP_OK);
  fault = 3;
  poll_once();
  assert(!snapshot().is_compatible && physical == SPI_BOARD_SAFE_VALUES);
  poll_once();
  assert(!snapshot().outputs[0].confirmed);
  assert(request_amplifier(true) == ESP_OK);
  poll_once();
  uint64_t epoch = snapshot().epoch;
  now += (int64_t)SPI_BOARD_LEASE_MS * 1000;
  assert(!snapshot().is_compatible);
  assert(request_usb(false) == ESP_ERR_INVALID_STATE);
  test_c5_expire();
  assert(physical == SPI_BOARD_SAFE_VALUES);
  poll_once();
  assert(snapshot().epoch != epoch && !snapshot().outputs[0].confirmed);

  // A new P4 identity always resets the remote outputs.
  assert(request_amplifier(true) == ESP_OK);
  poll_once();
  s_client++;
  reconnect();
  assert(physical == SPI_BOARD_SAFE_VALUES);
  for (int f = 4; f <= 7; f++) {
    invalidate(ESP_ERR_TIMEOUT);
    fault = f;
    poll_once();
    if (f == 5) {
      assert(snapshot().is_compatible);
      assert(request_amplifier(true) == ESP_ERR_NOT_SUPPORTED);
    } else {
      assert(!snapshot().is_compatible);
      assert(snapshot().link_result == (f == 4 ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_RESPONSE));
    }
    reconnect();
  }

  // Malformed commands and stale epochs cannot write GPIOs.
  uint8_t reply[sizeof(spi_board_caps_t)], reply_len;
  spi_board_set_t cmd = {.epoch = snapshot().epoch, .revision = SPI_BOARD_REVISION,
                        .control = SPI_BOARD_AMP, .value = 1};
  before = writes;
  assert(test_c5_rpc(SPI_ID_BOARD_SET, &cmd, sizeof(cmd)-1, reply, &reply_len) == SPI_STATUS_INVALID_ARG);
  cmd.value = 2;
  assert(test_c5_rpc(SPI_ID_BOARD_SET, &cmd, sizeof(cmd), reply, &reply_len) == SPI_STATUS_INVALID_ARG);
  cmd.value = 1; cmd.control = SPI_BOARD_CONTROL_COUNT;
  assert(test_c5_rpc(SPI_ID_BOARD_SET, &cmd, sizeof(cmd), reply, &reply_len) == SPI_STATUS_UNSUPPORTED);
  cmd.control = SPI_BOARD_AMP; cmd.epoch++;
  assert(test_c5_rpc(SPI_ID_BOARD_SET, &cmd, sizeof(cmd), reply, &reply_len) == SPI_STATUS_BUSY);
  assert(writes == before);
  assert(test_c5_rpc(SPI_ID_BOARD_SET, NULL, sizeof(cmd), reply, &reply_len) == SPI_STATUS_INVALID_ARG);

  spi_board_negotiate_t hello = {.protocol = SPI_PROTOCOL_VERSION,
      .revision = SPI_BOARD_REVISION, .model = SPI_BOARD_MODEL_V203,
      .client = s_client, .reserved = 1};
  assert(test_c5_rpc(SPI_ID_BOARD_NEGOTIATE, &hello, sizeof(hello), reply, &reply_len) == SPI_STATUS_INVALID_ARG);
  hello.reserved = 0; hello.model++;
  assert(test_c5_rpc(SPI_ID_BOARD_NEGOTIATE, &hello, sizeof(hello), reply, &reply_len) == SPI_STATUS_UNSUPPORTED);
  hello.model = SPI_BOARD_MODEL_V203; hello.client = 0;
  assert(test_c5_rpc(SPI_ID_BOARD_NEGOTIATE, &hello, sizeof(hello), reply, &reply_len) == SPI_STATUS_INVALID_ARG);
  assert(test_c5_rpc(SPI_ID_BOARD_STATE, &cmd, sizeof(cmd), reply, &reply_len) == SPI_STATUS_INVALID_ARG);
  assert(test_c5_rpc(SPI_ID_SYSTEM_PING, &cmd, sizeof(cmd), reply, &reply_len) == SPI_STATUS_UNSUPPORTED);
  assert(writes == before);

  fail_gpio = SPI_BOARD_CAP_AMP;
  assert(request_amplifier(true) == ESP_OK);
  poll_once();
  assert(!snapshot().is_compatible && snapshot().outputs[0].result == ESP_FAIL);
  fail_gpio = 0;
  test_c5_expire();
  poll_once();
  assert(snapshot().outputs[0].is_known && !snapshot().outputs[0].confirmed);
  assert(request_haptic(true) == ESP_OK);
  unsigned old_sets = set_calls;
  test_c5_reboot();
  assert(board_service_reconcile() == ESP_OK);
  assert(set_calls == old_sets && !snapshot().outputs[BOARD_CONTROL_HAPTIC].is_pending);
  assert(!snapshot().outputs[BOARD_CONTROL_HAPTIC].confirmed);
  atomic_flag_test_and_set(&s_poll_busy);
  assert(board_service_reconcile() == ESP_ERR_INVALID_STATE);
  atomic_flag_clear(&s_poll_busy);
  return 0;
}
