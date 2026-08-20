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
#include "tusb_msc_storage.h"

#include "tusb_desc.h"
#include "vfs_sdcard.h"

static const char *TAG = "USB_MSC";

#define USB_MSC_DETACH_FAIL_DELAY_MS  800
#define USB_MSC_REMOUNT_FAIL_DELAY_MS 500

static volatile usb_msc_state_t s_state = USB_MSC_IDLE;
static void *s_card = NULL;

usb_msc_state_t usb_msc_get_state(void) {
  return s_state;
}

bool usb_msc_host_connected(void) {
  return (s_state == USB_MSC_ACTIVE) && tinyusb_msc_storage_in_use_by_usb_host();
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

  const tinyusb_msc_sdmmc_config_t msc_cfg = {.card = (sdmmc_card_t *)raw};
  if (tinyusb_msc_storage_init_sdmmc(&msc_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "MSC storage init failed - restoring SD");
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
  tinyusb_msc_storage_deinit();
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
