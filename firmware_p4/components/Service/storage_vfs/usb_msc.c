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

#include "usb_msc.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

#include "tusb_desc.h"
#include "host_link.h"
#include "vfs_sdcard.h"

static const char *TAG = "USB_MSC";

#define USB_MSC_WAIT_MS           10
#define USB_MSC_ASC_NO_MEDIA      0x3A
#define USB_MSC_ASC_IO_ERROR      0x11
#define USB_MSC_ASC_WRITE_ERROR   0x0C
#define USB_MSC_ASC_INVALID_FIELD 0x24
#define USB_MSC_SYNCHRONIZE_CACHE 0x35

static _Atomic usb_msc_state_t s_state = USB_MSC_IDLE;
static atomic_bool s_quiesced = false;
static sdmmc_card_t *s_card = NULL;
static bool s_ejected = false;
static bool s_io_failed = false;

static void quiesce(void *arg);
static bool restore_sd(void);
static int32_t
transfer(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size, bool is_write);

usb_msc_state_t usb_msc_get_state(void) {
  return atomic_load(&s_state);
}

bool usb_msc_host_connected(void) {
  return atomic_load(&s_state) == USB_MSC_ACTIVE && tud_mounted();
}

void usb_msc_enter(void) {
  usb_msc_state_t expected = USB_MSC_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_ENTERING)) {
    if (expected != USB_MSC_ERROR ||
        !atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_ENTERING))
      return;
    if (s_card != NULL) {
      atomic_store(&s_state, USB_MSC_ERROR);
      return;
    }
  }
  void *card = NULL;
  esp_err_t err = vfs_sdcard_detach_for_msc(&card);
  if (err != ESP_OK || card == NULL) {
    ESP_LOGE(TAG, "Cannot give SD to USB: %s; close SD files first", esp_err_to_name(err));
    atomic_store(&s_state, USB_MSC_ERROR);
    return;
  }
  s_card = card;
  s_ejected = false;
  s_io_failed = false;
  busb_set_msc_exposed(true);
  err = usb_mux_set_native(true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "USB mux switch failed: %s", esp_err_to_name(err));
    atomic_store(&s_state, USB_MSC_ACTIVE);
    usb_msc_exit();
    return;
  }
  err = host_link_cdc_init();
  if (err != ESP_OK)
    ESP_LOGW(TAG, "Native CDC diagnostics unavailable: %s", esp_err_to_name(err));
  ESP_LOGI(TAG,
           "USB drive live: %lu sectors, %lu bytes/sector",
           (unsigned long)s_card->csd.capacity,
           (unsigned long)s_card->csd.sector_size);
  atomic_store(&s_state, USB_MSC_ACTIVE);
}

void usb_msc_exit(void) {
  usb_msc_state_t expected = USB_MSC_ACTIVE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_EXITING)) {
    if (expected == USB_MSC_ERROR &&
        atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_EXITING)) {
      if (s_card != NULL && !restore_sd())
        return;
      atomic_store(&s_state, USB_MSC_IDLE);
    }
    return;
  }
  busb_set_msc_exposed(false);
  if (tud_inited()) {
    atomic_store(&s_quiesced, false);
    usbd_defer_func(quiesce, NULL, false);
    while (!atomic_load(&s_quiesced))
      vTaskDelay(pdMS_TO_TICKS(USB_MSC_WAIT_MS));
  }
  esp_err_t err = usb_mux_set_native(false);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "UART mux restore failed: %s", esp_err_to_name(err));
  if (!restore_sd())
    return;
  atomic_store(&s_state, USB_MSC_IDLE);
  ESP_LOGI(TAG, "Left USB storage mode; /sdcard restored");
}

// esp_tinyusb bus events must not mount a filesystem owned by the host.
void msc_storage_mount_to_app(void) {}

void msc_storage_mount_to_usb(void) {}

uint8_t tud_msc_get_maxlun_cb(void) {
  return 1;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) {
  (void)lun;
  memcpy(vendor, "Highboy ", 8);
  memcpy(product, "Highboy Storage ", 16);
  memcpy(revision, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  if (lun == 0 && atomic_load(&s_state) == USB_MSC_ACTIVE && s_card != NULL && !s_ejected &&
      !s_io_failed)
    return true;
  tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, USB_MSC_ASC_NO_MEDIA, 0);
  return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size) {
  *count = 0;
  *size = 0;
  if (tud_msc_test_unit_ready_cb(lun)) {
    *count = s_card->csd.capacity;
    *size = s_card->csd.sector_size;
  }
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool eject) {
  (void)power;
  if (atomic_load(&s_state) != USB_MSC_ACTIVE || lun != 0 || s_card == NULL)
    return false;
  if (eject)
    s_ejected = !start;
  return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  return transfer(lun, lba, offset, buffer, size, false);
}

int32_t
tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
  return transfer(lun, lba, offset, buffer, size, true);
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t cmd[16], void *buffer, uint16_t size) {
  (void)buffer;
  (void)size;
  if (!tud_msc_test_unit_ready_cb(lun))
    return -1;
  if (cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL)
    return 0;
  if (cmd[0] == USB_MSC_SYNCHRONIZE_CACHE) {
    // SDMMC writes complete before WRITE10 returns; there is no write-behind cache.
    if (sdmmc_get_status(s_card) == ESP_OK)
      return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, USB_MSC_ASC_IO_ERROR, 0);
    return -1;
  }
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, USB_MSC_ASC_INVALID_FIELD, 0);
  return -1;
}

static int32_t
transfer(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size, bool is_write) {
  if (!tud_msc_test_unit_ready_cb(lun))
    return -1;
  uint32_t sector_size = s_card->csd.sector_size;
  uint64_t address = (uint64_t)lba * sector_size + offset;
  uint64_t capacity = (uint64_t)s_card->csd.capacity * sector_size;
  if (sector_size == 0 || buffer == NULL || address % sector_size != 0 || size % sector_size != 0 ||
      address > capacity || size > capacity - address) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, USB_MSC_ASC_INVALID_FIELD, 0);
    return -1;
  }
  if (size == 0)
    return 0;
  esp_err_t err = ESP_OK;
  if (is_write) {
    // IDF 6.1 multi-block error recovery can replace the write error with CMD22 success.
    for (uint32_t done = 0; done < size; done += sector_size) {
      err = sdmmc_write_sectors(
          s_card, (const uint8_t *)buffer + done, (address + done) / sector_size, 1);
      if (err != ESP_OK)
        break;
    }
  } else {
    err = sdmmc_read_sectors(s_card, buffer, address / sector_size, size / sector_size);
  }
  if (err != ESP_OK) {
    s_io_failed = true;
    ESP_LOGE(TAG,
             "%s lba=%lu bytes=%lu failed: %s",
             is_write ? "WRITE" : "READ",
             (unsigned long)lba,
             (unsigned long)size,
             esp_err_to_name(err));
    tud_msc_set_sense(
        lun, SCSI_SENSE_MEDIUM_ERROR, is_write ? USB_MSC_ASC_WRITE_ERROR : USB_MSC_ASC_IO_ERROR, 0);
    return -1;
  }
  return size;
}

static void quiesce(void *arg) {
  (void)arg;
  // The USB task has completed every synchronous SD operation preceding this barrier.
  atomic_store(&s_quiesced, true);
}

static bool restore_sd(void) {
  esp_err_t err = vfs_sdcard_reattach_after_msc(s_card);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "SD recovery failed: %s; USB stopped, SD unavailable until restart",
             esp_err_to_name(err));
    atomic_store(&s_state, USB_MSC_SD_UNAVAILABLE);
    return false;
  }
  s_card = NULL;
  return true;
}
