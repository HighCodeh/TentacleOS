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
#include <string.h>

#include "aw8623.c"

static int fail_read = -1, read_count, remove_fail, epoch_fail, release_fail, reset_fail;
static uint8_t chip_id = AW8623_CHIP_ID;
static bool released;
static int removed, added;
static int is_isr;
static int64_t now_us, end_us;
static int go_writes, writes, fail_write = -1;
static bool lose_go_ack, stuck_go, cancel_play, lose_epoch_on_go;
static bool cancel_on_go, fault_on_go;
static uint8_t fault_status;
static uint8_t registers[256], ram[8192];
static uint16_t ram_address;
int64_t esp_timer_get_time(void) { return now_us; }
static bool cancelled(void) { return cancel_play; }
esp_err_t i2c_master_transmit(void *device, const void *data, size_t size, int timeout) {
  assert(device != NULL && released && timeout == 100 && size >= 2);
  if (writes++ == fail_write) return ESP_FAIL;
  const uint8_t *bytes = data;
  uint8_t reg = bytes[0];
  if (reg == AW8623_RAMDATA) {
    assert(ram_address + size - 1 <= sizeof(ram));
    memcpy(ram + ram_address, bytes + 1, size - 1); ram_address += size - 1;
  } else {
    assert(size == 2); registers[reg] = bytes[1];
    if (reg == AW8623_RAMADDRH) ram_address = (ram_address & 0xff) | ((uint16_t)bytes[1] << 8);
    if (reg == AW8623_RAMADDRL) ram_address = (ram_address & 0xff00) | bytes[1];
    if (reg == AW8623_GO && bytes[1] == 1) {
      if (cancel_on_go) cancel_play = true;
      if (fault_on_go) fault_status = 4;
      go_writes++; end_us = now_us + 20000;
      if (lose_epoch_on_go) epoch_fail = 1;
      if (lose_go_ack) return ESP_ERR_TIMEOUT;
    }
  }
  return ESP_OK;
}

int xPortInIsrContext(void) { return is_isr; }
void vTaskDelay(int ticks) { assert(ticks == AW8623_RESET_SETTLE_MS); now_us += ticks * 1000; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "mock"; }
i2c_master_bus_handle_t i2c_get_bus(void) { return (void *)1; }
esp_err_t i2c_master_bus_add_device(void *bus, const i2c_device_config_t *cfg, void **out) {
  assert(bus != NULL && cfg->device_address == 0x5A); added++; *out = (void *)2; return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(void *device) {
  assert(device != NULL); removed++; return remove_fail ? ESP_FAIL : ESP_OK;
}
esp_err_t i2c_master_transmit_receive(void *device, const void *tx, size_t tx_len,
                                    void *rx, size_t rx_len, int timeout) {
  assert(device != NULL && released && tx_len == 1 && rx_len == 1 && timeout == 100);
  if (read_count++ == fail_read) return ESP_FAIL;
  uint8_t reg = *(const uint8_t *)tx;
  assert(reg <= AW8623_INTERRUPT_REG || reg == AW8623_GO);
  *(uint8_t *)rx = reg == AW8623_ID_REG ? chip_id :
      reg == AW8623_GO ? (stuck_go || now_us < end_us) : (0x10 | fault_status);
  return ESP_OK;
}
static esp_err_t status(board_control_status_t *out) {
  memset(out, 0, sizeof(*out)); out->is_compatible = true;
  out->epoch = epoch_fail ? 2 : 1;
  out->outputs[BOARD_CONTROL_HAPTIC].is_known = true;
  out->outputs[BOARD_CONTROL_HAPTIC].confirmed = released;
  out->is_haptic_irq_pending = true;
  return ESP_OK;
}
static const board_control_ops_t OPS = {.get_status = status};
esp_err_t board_control_apply(const board_control_ops_t *ops, board_control_id_t control,
                              bool value, uint64_t *epoch) {
  if (ops == NULL) return ESP_ERR_NOT_SUPPORTED;
  assert(control == BOARD_CONTROL_HAPTIC);
  if (value && release_fail) { released = true; return ESP_ERR_TIMEOUT; }
  if (!value && reset_fail) return ESP_ERR_TIMEOUT;
  released = value;
  if (!value) registers[AW8623_GO] = 0;
  if (epoch != NULL) *epoch = 1;
  return ESP_OK;
}
int main(void) {
  aw8623_diagnostic_t diagnostic;
  assert(aw8623_probe(&OPS, NULL) == ESP_ERR_INVALID_ARG);
  assert(aw8623_probe(NULL, &diagnostic) == ESP_ERR_NOT_SUPPORTED);
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_OK);
  assert(diagnostic.is_identified && diagnostic.is_reset_confirmed && diagnostic.has_irq_event);
  assert(!released && added == 1 && removed == 1 && s_device == NULL);
  chip_id = 0x60; read_count = 0;
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_ERR_NOT_SUPPORTED);
  assert(!diagnostic.is_identified && diagnostic.is_reset_confirmed && read_count == 1);
  chip_id = AW8623_CHIP_ID;
  for (fail_read = 0; fail_read < 3; fail_read++) {
    read_count = 0;
    assert(aw8623_probe(&OPS, &diagnostic) == ESP_FAIL);
    assert(!released && diagnostic.is_reset_confirmed);
  }
  fail_read = -1; epoch_fail = 1;
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_ERR_INVALID_STATE);
  assert(!released); epoch_fail = 0; release_fail = 1;
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_ERR_TIMEOUT);
  assert(!released); release_fail = 0; reset_fail = 1;
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_ERR_TIMEOUT);
  assert(!diagnostic.is_reset_confirmed); reset_fail = 0; remove_fail = 1;
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_FAIL);
  assert(s_device != NULL); int allocations = added; remove_fail = 0;
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_OK);
  assert(added == allocations && s_device == NULL);
  atomic_flag_test_and_set(&s_busy);
  assert(aw8623_stop(&OPS) == ESP_ERR_INVALID_STATE);
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_ERR_INVALID_STATE);
  atomic_flag_clear(&s_busy);
  is_isr = 1;
  assert(aw8623_probe(&OPS, &diagnostic) == ESP_ERR_INVALID_STATE);
  assert(aw8623_stop(&OPS) == ESP_ERR_INVALID_STATE);
  is_isr = 0;
  assert(aw8623_stop(&OPS) == ESP_OK);
  aw8623_playback_t request = {.intensity = 100, .duration_ms = 500, .is_cancelled = cancelled};
  read_count = 0;
  assert(aw8623_play(&OPS, &request) == ESP_OK);
  assert(go_writes == 1 && !released);
  assert(ram[0x800] == 1 && ram[0x801] == 8 && ram[0x802] == 5);
  assert(ram[0x803] == 0x1f && ram[0x804] == 0x74);
  for (int i = 0x805; i <= 0x1f74; i++) assert(ram[i] == 69);
  assert(registers[0x31] == 0 && registers[0x39] == 0 && registers[0x3b] == 0x80);
  assert(registers[0x0f] == 0 && registers[0x13] == 0);
  int total_writes = writes;
  for (int i = 0; i < total_writes; i++) {
    writes = 0; fail_write = i; go_writes = 0;
    assert(aw8623_play(&OPS, &request) == ESP_FAIL);
    assert(go_writes <= 1 && !released);
  }
  fail_write = -1; go_writes = 0; lose_go_ack = true;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_TIMEOUT);
  assert(go_writes == 1 && !released); lose_go_ack = false;
  stuck_go = true;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_TIMEOUT);
  assert(!released); stuck_go = false;
  cancel_play = true; go_writes = 0;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_INVALID_STATE);
  assert(go_writes == 0 && !released); cancel_play = false;
  lose_epoch_on_go = true;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_INVALID_STATE);
  assert(!released); epoch_fail = 0; lose_epoch_on_go = false;
  fault_status = 4; go_writes = 0;
  assert(aw8623_play(&OPS, &request) == ESP_FAIL);
  assert(go_writes == 0 && !released); fault_status = 0;
  cancel_on_go = true; go_writes = 0;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_INVALID_STATE);
  assert(go_writes == 1 && !released); cancel_on_go = false; cancel_play = false;
  fault_on_go = true;
  assert(aw8623_play(&OPS, &request) == ESP_FAIL);
  assert(!released); fault_on_go = false; fault_status = 0;
  request.duration_ms = 501;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_INVALID_ARG);
  request.duration_ms = 0;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_INVALID_ARG);
  request.duration_ms = 200; request.intensity = 101;
  assert(aw8623_play(&OPS, &request) == ESP_ERR_INVALID_ARG);
  puts("PASS: finite RAM playback, amplitude, every write failure, lost GO ACK, cancellation, epoch loss, faults and timeout");
  puts("PASS: AW8623 non-driving identification, I2C faults, epoch loss, lost release ACK, reset failure and retained I2C handle");
}
