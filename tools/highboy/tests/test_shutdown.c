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

#include "bq25896.c"

#include <assert.h>
#include <stdio.h>

#define TEST_LONG_READING_MS 1200U
#define TEST_REG03_START 0xBAU
#define TEST_REG09_START 0x54U
#define NOTIFY_WARNING 1

static uint8_t s_regs[BQ25896_REGISTER_COUNT];
static uint8_t s_before[BQ25896_REGISTER_COUNT];
static bool s_is_locked, s_fail_lock, s_fail_gpio, s_fail_create, s_is_usb;
static int s_fail_read_reg = -1, s_fail_write_reg = -1;
static uint32_t s_busy_ms, s_wait_ms, s_writes, s_reads_after_cut;
static int64_t s_time_us;
static bool s_shutdown_started, s_bridge_busy, s_is_maintenance;
static uint32_t s_prepare_count, s_screen_count, s_unmount_count, s_failure_count;
static char s_notification[96];

const char *esp_err_to_name(int error) {
  return error == ESP_ERR_TIMEOUT ? "ESP_ERR_TIMEOUT" : "error";
}
void *xSemaphoreCreateMutex(void) { return s_fail_create ? NULL : (void *)1; }
int xSemaphoreTake(void *handle, uint32_t ticks) {
  assert(handle != NULL && !s_is_locked);
  s_wait_ms = ticks;
  if (s_fail_lock || s_busy_ms > ticks)
    return 0;
  s_is_locked = true;
  return pdTRUE;
}
void xSemaphoreGive(void *handle) { assert(handle != NULL && s_is_locked); s_is_locked = false; }
int64_t esp_timer_get_time(void) { return s_time_us; }
void vTaskDelay(uint32_t ticks) { s_time_us += ticks * 1000U; }
int gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
int gpio_set_level(int pin, int level) {
  assert(pin == GPIO_CHARGER_OTG_PIN && level == 0);
  return s_fail_gpio ? ESP_FAIL : ESP_OK;
}
int gpio_get_level(int pin) { (void)pin; return 0; }
void *i2c_get_bus(void) { return (void *)1; }
int i2c_bus_recover(void) { return ESP_OK; }
int i2c_master_bus_add_device(void *bus, const i2c_device_config_t *config, void **out) {
  assert(bus != NULL && config != NULL); *out = (void *)1; return ESP_OK;
}
int i2c_master_transmit(void *device, const uint8_t *data, size_t count, int timeout) {
  assert(device == s_dev && s_is_locked && count == 2 && timeout > 0);
  uint8_t reg = data[0];
  assert(reg < BQ25896_REGISTER_COUNT);
  if (reg == s_fail_write_reg)
    return ESP_FAIL;
  s_regs[reg] = data[1];
  s_writes++;
  return ESP_OK;
}
int i2c_master_transmit_receive(void *device, const uint8_t *reg, size_t count,
                               uint8_t *out, size_t length, int timeout) {
  assert(device == s_dev && s_is_locked && count == 1 && length == 1 && timeout > 0);
  if (s_regs[REG09_MISC_CONTROL] & REG09_BATFET_DIS)
    s_reads_after_cut++;
  if (*reg == s_fail_read_reg)
    return ESP_FAIL;
  *out = s_regs[*reg];
  return ESP_OK;
}
static int spi_bridge_maintenance_begin(void) {
  if (s_bridge_busy)
    return ESP_ERR_INVALID_STATE;
  assert(!s_is_maintenance); s_is_maintenance = true; return ESP_OK;
}
static void spi_bridge_maintenance_end(void) { assert(s_is_maintenance); s_is_maintenance = false; }
static bool external_power_present(void) { return s_is_usb; }
static void notify(int kind, const char *message) {
  assert(kind == NOTIFY_WARNING); s_failure_count++;
  snprintf(s_notification, sizeof(s_notification), "%s", message);
}
static void led_signal_warning(void) {}
static void led_signal_error(void) {}
static void ui_prepare_shutdown(void) {
  assert(s_is_maintenance && (s_regs[REG09_MISC_CONTROL] & REG09_BATFET_DIS));
  s_prepare_count++;
}
static void show_shutdown_screen(bool usb, const char *reason) {
  assert(usb == s_is_usb && reason != NULL && s_prepare_count == 1); s_screen_count++;
}
static void audio_i2s_set_volume(int volume) { assert(volume == 0); }
static void audio_i2s_stream_stop(void) {}
static void audio_i2s_mic_stream_stop(void) {}
static void lf_rfid_field_stop(void) {}
static void led_clear(void) {}
static bool storage_is_mounted(void) { return true; }
static int storage_deinit(void) { assert(s_prepare_count == 1 && s_screen_count == 1); s_unmount_count++; return ESP_OK; }

/* SHUTDOWN_POLICY */

static void reset(void) {
  for (size_t i = 0; i < sizeof(s_regs); i++)
    s_regs[i] = (uint8_t)i;
  s_regs[REG03_POWER_CONTROL] = TEST_REG03_START;
  s_regs[REG09_MISC_CONTROL] = TEST_REG09_START;
  memcpy(s_before, s_regs, sizeof(s_regs));
  s_dev = (void *)1; s_lock = (void *)1;
  s_is_locked = s_fail_lock = s_fail_gpio = s_fail_create = false;
  s_fail_read_reg = s_fail_write_reg = -1;
  s_busy_ms = s_writes = s_reads_after_cut = 0;
  s_shutdown_started = s_bridge_busy = s_is_maintenance = s_is_usb = false;
  s_prepare_count = s_screen_count = s_unmount_count = s_failure_count = 0;
  s_notification[0] = '\0';
}
static void unchanged_charge_profile(void) {
  for (size_t i = 0; i < sizeof(s_regs); i++) {
    if (i != REG03_POWER_CONTROL && i != REG09_MISC_CONTROL)
      assert(s_regs[i] == s_before[i]);
  }
  assert((s_regs[REG03_POWER_CONTROL] & (uint8_t)~REG03_OTG_CONFIG) ==
         (s_before[REG03_POWER_CONTROL] & (uint8_t)~REG03_OTG_CONFIG));
}
static void not_shut_down(void) {
  assert(!s_shutdown_started && !s_is_maintenance && !s_is_locked);
  assert(!s_prepare_count && !s_screen_count && !s_unmount_count);
  assert(!(s_regs[REG09_MISC_CONTROL] & REG09_BATFET_DIS));
  assert(s_failure_count == 1); unchanged_charge_profile();
}
int main(void) {
  reset();
  // Reproduce the actual manual route using the board's compiled charger policy.
  begin_shutdown("Manual");
  assert(s_shutdown_started && s_is_maintenance && s_unmount_count == 1);
  assert(s_regs[REG09_MISC_CONTROL] ==
         ((TEST_REG09_START & (uint8_t)~(REG09_BATFET_RST_EN | REG09_BATFET_DIS | REG09_BATFET_DLY)) |
          REG09_BATFET_DIS | REG09_BATFET_DLY));
  assert(!(s_regs[REG03_POWER_CONTROL] & REG03_OTG_CONFIG));
  assert(s_reads_after_cut == 0); unchanged_charge_profile();
  uint32_t writes = s_writes; begin_shutdown("Duplicate"); assert(s_writes == writes);
  reset(); s_is_usb = true; begin_shutdown("USB");
  assert(s_shutdown_started && s_screen_count == 1 && s_unmount_count == 1);
  reset(); s_bridge_busy = true; begin_shutdown("Busy"); not_shut_down(); assert(s_writes == 0);
  reset(); s_fail_lock = true; begin_shutdown("Locked"); not_shut_down();
  s_fail_lock = false; begin_shutdown("Retry"); assert(s_shutdown_started);
  reset(); s_fail_gpio = true; begin_shutdown("GPIO"); not_shut_down();
  for (size_t i = 0; i < 2; i++) {
    int reg = i == 0 ? REG03_POWER_CONTROL : REG09_MISC_CONTROL;
    reset(); s_fail_read_reg = reg; begin_shutdown("Read error"); not_shut_down();
    reset(); s_fail_write_reg = reg; begin_shutdown("Write error"); not_shut_down();
  }
  reset(); s_busy_ms = TEST_LONG_READING_MS; begin_shutdown("Reading in progress");
  assert(s_shutdown_started && s_wait_ms >= TEST_LONG_READING_MS);
  reset(); s_lock = NULL; s_fail_create = true;
  assert(bq25896_power_off() == ESP_ERR_NO_MEM && !s_writes);
#ifdef CONFIG_HIGHBOY_BOARD_V203
  reset(); assert(lock_driver() == ESP_OK);
  assert(write_reg_unlocked(REG09_MISC_CONTROL, 0) == ESP_ERR_NOT_SUPPORTED);
  assert(write_reg_unlocked(REG04_CHARGE_CURRENT, 0x3f) == ESP_ERR_NOT_SUPPORTED);
  assert(write_reg_unlocked(REG06_CHARGE_VOLTAGE, 0xff) == ESP_ERR_NOT_SUPPORTED);
  unlock_driver(); assert(s_writes == 0);
  assert(bq25896_set_otg_enable(true) == ESP_ERR_NOT_SUPPORTED && s_writes == 0);
  puts("PASS: V2.03 shutdown reaches BATFET; charge/OTG guards retained, errors preserve UI/storage, bounded ADC contention and retry");
#else
  puts("PASS: generic shutdown sequencing, transport/GPIO failures, USB path and retry");
#endif
}
