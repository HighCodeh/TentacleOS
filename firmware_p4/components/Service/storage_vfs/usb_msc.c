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

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "tinyusb_msc.h"

#include "tusb_desc.h"
#include "vfs_sdcard.h"

static const char *TAG = "USB_MSC";

#define USB_MSC_DETACH_FAIL_DELAY_MS  800
#define USB_MSC_REMOUNT_FAIL_DELAY_MS 500

static volatile usb_msc_state_t s_state = USB_MSC_IDLE;
static void *s_card = NULL;
static tinyusb_msc_storage_handle_t s_handle = NULL;

usb_msc_state_t usb_msc_get_state(void) {
  return s_state;
}

bool usb_msc_host_connected(void) {
  if (s_state != USB_MSC_ACTIVE || s_handle == NULL) {
    return false;
  }
  tinyusb_msc_mount_point_t mp;
  return tinyusb_msc_get_storage_mount_point(s_handle, &mp) == ESP_OK &&
         mp == TINYUSB_MSC_STORAGE_MOUNT_USB;
}

void usb_msc_enter(void) {
  s_state = USB_MSC_ENTERING;

  void *raw = NULL;
  if (vfs_sdcard_detach_for_msc(&raw) != ESP_OK || raw == NULL) {
    ESP_LOGE(TAG, "SD detach failed - rebooting to recover");
    vTaskDelay(pdMS_TO_TICKS(USB_MSC_DETACH_FAIL_DELAY_MS));
    esp_restart();
  }
  s_card = raw;

  // We own the card lifecycle ourselves (detach/reattach above), so disable the
  // MSC layer's auto mount/format on host connect+eject: otherwise it remounts
  // (and may format) the FAT inside the device during the host's eject flush,
  // fighting our handoff and rebooting the device.
  const tinyusb_msc_driver_config_t drv_cfg = {.user_flags.auto_mount_off = 1};
  esp_err_t dr = tinyusb_msc_install_driver(&drv_cfg);
  if (dr != ESP_OK && dr != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "MSC driver install failed (%s) - restoring SD", esp_err_to_name(dr));
    (void)vfs_sdcard_reattach_after_msc(s_card);
    s_card = NULL;
    s_state = USB_MSC_ERROR;
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
    (void)vfs_sdcard_reattach_after_msc(s_card);
    s_card = NULL;
    s_state = USB_MSC_ERROR;
    return;
  }

  busb_set_msc_exposed(true);
  usb_mux_set_native(true);
  s_state = USB_MSC_ACTIVE;
  ESP_LOGI(TAG, "USB drive live");
}

void usb_msc_exit(void) {
  if (s_state != USB_MSC_ACTIVE) {
    s_state = USB_MSC_IDLE;
    return;
  }
  s_state = USB_MSC_EXITING;

  // Hide MSC (re-enumerating the host) while the storage handle is still valid,
  // so no SCSI command lands on a deinitialized backend.
  busb_set_msc_exposed(false);
  if (s_handle != NULL) {
    esp_err_t dr = tinyusb_msc_delete_storage(s_handle);
    if (dr != ESP_OK) {
      ESP_LOGW(TAG, "MSC storage delete failed: %s", esp_err_to_name(dr));
    }
    s_handle = NULL;
  }
  usb_mux_set_native(false);

  esp_err_t r = vfs_sdcard_reattach_after_msc(s_card);
  s_card = NULL;
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "SD remount failed (%s) - rebooting to recover", esp_err_to_name(r));
    vTaskDelay(pdMS_TO_TICKS(USB_MSC_REMOUNT_FAIL_DELAY_MS));
    esp_restart();
  }

  s_state = USB_MSC_IDLE;
  ESP_LOGI(TAG, "Left USB storage mode; /sdcard restored");
}
