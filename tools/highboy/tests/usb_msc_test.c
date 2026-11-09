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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "usb_msc.h"
typedef int esp_err_t;
typedef struct { struct { uint32_t capacity, sector_size; } csd; } sdmmc_card_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define SCSI_SENSE_NOT_READY 2
#define SCSI_SENSE_MEDIUM_ERROR 3
#define SCSI_SENSE_ILLEGAL_REQUEST 5
#define SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1e
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
static sdmmc_card_t card = {.csd = {2097154048, 512}};
static bool owned, exposed, mounted, initialized, in_flight;
static int detach_error, restore_error, mux_error, io_error, sense, reads, writes;
static uint32_t last_lba;
static int fail_write_call, restore_calls;
static void (*pending)(void *);
static void *pending_arg;
const char *esp_err_to_name(int err) { (void)err; return "test"; }
bool tud_inited(void) { return initialized; }
bool tud_mounted(void) { return mounted; }
void tud_msc_set_sense(uint8_t lun, int key, int asc, int ascq) {
  (void)lun; (void)asc; (void)ascq; sense = key;
}
void usbd_defer_func(void (*cb)(void *), void *arg, bool isr) {
  assert(!isr && pending == NULL); pending = cb; pending_arg = arg;
}
void vTaskDelay(uint32_t delay) {
  assert(delay > 0 && owned);
  if (pending) {
    in_flight = false;
    void (*cb)(void *) = pending; pending = NULL; cb(pending_arg);
  }
}
int vfs_sdcard_detach_for_msc(void **out) {
  usb_msc_enter(); usb_msc_exit();
  assert(usb_msc_get_state() == USB_MSC_ENTERING);
  if (detach_error) return detach_error;
  assert(!owned); owned = true; *out = &card; return ESP_OK;
}
int vfs_sdcard_reattach_after_msc(void *raw) {
  restore_calls++;
  assert(raw == &card && owned && !exposed && !in_flight);
  usb_msc_enter(); usb_msc_exit();
  assert(usb_msc_get_state() == USB_MSC_EXITING);
  if (restore_error) return restore_error;
  owned = false; return ESP_OK;
}
void busb_set_msc_exposed(bool value) { exposed = value; }
int usb_mux_set_native(bool value) { return value ? mux_error : ESP_OK; }
int host_link_cdc_init(void) { return ESP_OK; }
int sdmmc_get_status(sdmmc_card_t *raw) { assert(raw == &card && owned); return io_error; }
int sdmmc_read_sectors(sdmmc_card_t *raw, void *buf, uint32_t lba, uint32_t count) {
  assert(raw == &card && owned && buf && count > 0); reads++; last_lba = lba; return io_error;
}
int sdmmc_write_sectors(sdmmc_card_t *raw, const void *buf, uint32_t lba, uint32_t count) {
  assert(raw == &card && owned && buf && count > 0); writes++; last_lba = lba; assert(count == 1);
  return writes == fail_write_call ? ESP_FAIL : io_error;
}
#include "../../../firmware_p4/components/Service/storage_vfs/usb_msc.c"

int main(void) {
  uint8_t data[1024] = {0}, cmd[16] = {0};
  uint32_t count; uint16_t size;
  assert(!tud_msc_test_unit_ready_cb(0));
  for (int i = 0; i < 10; i++) {
    initialized = i != 0;
    usb_msc_enter(); assert(owned && exposed && usb_msc_get_state() == USB_MSC_ACTIVE);
    mounted = true; assert(usb_msc_host_connected());
    usb_msc_enter();
    tud_msc_capacity_cb(0, &count, &size); assert(count == card.csd.capacity && size == 512);
    assert(tud_msc_read10_cb(0, 8, 512, data, sizeof(data)) == sizeof(data)); assert(last_lba == 9);
    assert(tud_msc_write10_cb(0, count - 2, 0, data, sizeof(data)) == sizeof(data));
    in_flight = initialized;
    usb_msc_exit(); assert(!owned && !exposed && usb_msc_get_state() == USB_MSC_IDLE);
  }
  detach_error = ESP_FAIL; usb_msc_enter(); assert(!owned && usb_msc_get_state() == USB_MSC_ERROR);
  usb_msc_exit(); detach_error = 0;
  mux_error = ESP_FAIL; usb_msc_enter(); assert(!owned && !exposed); mux_error = 0;
  usb_msc_enter();
  assert(tud_msc_read10_cb(0, card.csd.capacity, 0, data, sizeof(data)) == -1);
  assert(tud_msc_read10_cb(0, 0, 1, data, sizeof(data)) == -1);
  assert(tud_msc_write10_cb(0, UINT32_MAX, UINT32_MAX, data, sizeof(data)) == -1);
  assert(tud_msc_read10_cb(0, 0, 0, data, 1) == -1);
  assert(sense == SCSI_SENSE_ILLEGAL_REQUEST);
  fail_write_call = writes + 2;
  assert(tud_msc_write10_cb(0, 0, 0, data, sizeof(data)) == -1);
  assert(sense == SCSI_SENSE_MEDIUM_ERROR);
  int io_count = reads + writes;
  for (int i = 0; i < 20; i++) {
    assert(tud_msc_read10_cb(0, 0, 0, data, sizeof(data)) == -1);
    assert(tud_msc_write10_cb(0, 0, 0, data, sizeof(data)) == -1);
  }
  assert(reads + writes == io_count);
  fail_write_call = 0;
  usb_msc_exit(); usb_msc_enter();
  io_error = ESP_FAIL;
  assert(tud_msc_read10_cb(0, 0, 0, data, sizeof(data)) == -1);
  assert(sense == SCSI_SENSE_MEDIUM_ERROR);
  usb_msc_exit(); usb_msc_enter();
  cmd[0] = USB_MSC_SYNCHRONIZE_CACHE;
  assert(tud_msc_scsi_cb(0, cmd, NULL, 0) == -1);
  io_error = 0; assert(tud_msc_scsi_cb(0, cmd, NULL, 0) == 0);
  cmd[0] = SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL; assert(tud_msc_scsi_cb(0, cmd, NULL, 0) == 0);
  cmd[0] = 0xff; assert(tud_msc_scsi_cb(0, cmd, NULL, 0) == -1);
  assert(tud_msc_start_stop_cb(0, 0, false, true)); assert(!tud_msc_test_unit_ready_cb(0));
  assert(tud_msc_write10_cb(0, 0, 0, data, sizeof(data)) == -1);
  assert(tud_msc_start_stop_cb(0, 0, true, true)); assert(tud_msc_test_unit_ready_cb(0));
  restore_error = ESP_FAIL; usb_msc_exit();
  assert(owned && !exposed && usb_msc_get_state() == USB_MSC_SD_UNAVAILABLE);
  int attempts = restore_calls;
  for (int i = 0; i < 20; i++) {
    usb_msc_enter(); usb_msc_exit();
    assert(usb_msc_get_state() == USB_MSC_SD_UNAVAILABLE);
    assert(!tud_msc_test_unit_ready_cb(0));
  }
  assert(restore_calls == attempts && !exposed && owned);
  assert(reads > 0 && writes > 0);
  puts("PASS: MSC lifecycle, bounds, synchronous I/O errors, flush and eject");
}
