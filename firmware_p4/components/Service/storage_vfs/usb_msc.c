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

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "tinyusb_msc.h"
#include "tusb.h"

#include "tusb_desc.h"
#include "vfs_sdcard.h"

static const char *TAG = "USB_MSC";

#define USB_MSC_DETACH_FAIL_DELAY_MS  800
#define USB_MSC_REMOUNT_FAIL_DELAY_MS 500
#define USB_MSC_DELETE_RETRY_MS       10
#define USB_MSC_DELETE_ATTEMPTS       100

static _Atomic usb_msc_state_t s_state = USB_MSC_IDLE;
static void *s_card = NULL;
static tinyusb_msc_storage_handle_t s_handle = NULL;

static bool restore_sd(void);
static esp_err_t delete_storage(void);

usb_msc_state_t usb_msc_get_state(void) {
  return atomic_load(&s_state);
}

bool usb_msc_host_connected(void) {
  // Do not dereference the storage handle while the exit worker may delete it.
  return atomic_load(&s_state) == USB_MSC_ACTIVE && tud_mounted();
}

void usb_msc_enter(void) {
  usb_msc_state_t expected = USB_MSC_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_ENTERING)) {
    if (expected != USB_MSC_ERROR ||
        !atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_ENTERING))
      return;
  }

  void *raw = NULL;
  if (vfs_sdcard_detach_for_msc(&raw) != ESP_OK || raw == NULL) {
    ESP_LOGE(TAG, "SD detach failed - rebooting to recover");
    vTaskDelay(pdMS_TO_TICKS(USB_MSC_DETACH_FAIL_DELAY_MS));
    esp_restart();
    return;
  }
  s_card = raw;

  // The VFS owns mount/format policy, including during a host eject flush.
  const tinyusb_msc_driver_config_t drv_cfg = {.user_flags.auto_mount_off = 1};
  esp_err_t err = tinyusb_msc_install_driver(&drv_cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "MSC driver install failed: %s", esp_err_to_name(err));
    (void)restore_sd();
    atomic_store(&s_state, USB_MSC_ERROR);
    return;
  }

  const tinyusb_msc_storage_config_t msc_cfg = {
      .medium.card = (sdmmc_card_t *)raw,
      .fat_fs = {.base_path = NULL, .do_not_format = true},
      .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
  };
  if (tinyusb_msc_new_storage_sdmmc(&msc_cfg, &s_handle) != ESP_OK) {
    ESP_LOGE(TAG, "MSC storage init failed - restoring SD");
    s_handle = NULL;
    (void)restore_sd();
    atomic_store(&s_state, USB_MSC_ERROR);
    return;
  }

  busb_set_msc_exposed(true);
  err = usb_mux_set_native(true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "USB mux switch failed: %s", esp_err_to_name(err));
    busb_set_msc_exposed(false);
    if (delete_storage() != ESP_OK) {
      busb_set_msc_exposed(true);
      atomic_store(&s_state, USB_MSC_ACTIVE);
      return;
    }
    (void)restore_sd();
    atomic_store(&s_state, USB_MSC_ERROR);
    return;
  }
  atomic_store(&s_state, USB_MSC_ACTIVE);
  ESP_LOGI(TAG, "USB drive live");
}

void usb_msc_exit(void) {
  usb_msc_state_t expected = USB_MSC_ACTIVE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_EXITING)) {
    if (expected == USB_MSC_ERROR)
      (void)atomic_compare_exchange_strong(&s_state, &expected, USB_MSC_IDLE);
    return;
  }

  // Hide MSC (re-enumerating the host) while the storage handle is still valid,
  // so no SCSI command lands on a deinitialized backend.
  busb_set_msc_exposed(false);
  esp_err_t err = delete_storage();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "MSC storage still owns SD (%s); eject can be retried", esp_err_to_name(err));
    busb_set_msc_exposed(true);
    atomic_store(&s_state, USB_MSC_ACTIVE);
    return;
  }
  err = usb_mux_set_native(false);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "USB UART mux restore failed: %s", esp_err_to_name(err));

  if (!restore_sd())
    return;

  atomic_store(&s_state, USB_MSC_IDLE);
  ESP_LOGI(TAG, "Left USB storage mode; /sdcard restored");
}

static bool restore_sd(void) {
  esp_err_t r = vfs_sdcard_reattach_after_msc(s_card);
  s_card = NULL;
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "SD remount failed (%s) - rebooting to recover", esp_err_to_name(r));
    vTaskDelay(pdMS_TO_TICKS(USB_MSC_REMOUNT_FAIL_DELAY_MS));
    esp_restart();
    return false;
  }
  return true;
}

static esp_err_t delete_storage(void) {
  esp_err_t err = ESP_ERR_INVALID_STATE;
  for (uint32_t attempt = 0; attempt < USB_MSC_DELETE_ATTEMPTS; attempt++) {
    err = tinyusb_msc_delete_storage(s_handle);
    if (err == ESP_OK) {
      s_handle = NULL;
      return ESP_OK;
    }
    if (err != ESP_ERR_INVALID_STATE)
      break;
    // The MSC worker must finish deferred writes before releasing the medium.
    vTaskDelay(pdMS_TO_TICKS(USB_MSC_DELETE_RETRY_MS));
  }
  return err;
}
