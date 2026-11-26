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

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "boot_report.h"
#include "kernel.h"
#include "console_service.h"
#include "ota_service.h"
#include "ui_manager.h"

void app_main(void) {
  // First: update the consecutive-abnormal-boot counter from the reset reason.
  // kernel_init drops into safe mode when this reports a boot loop.
  boot_report_track_bootloop();

  // kernel_init aborts to safe mode on a required-subsystem failure or a boot
  // loop, and reports it via the return code; the boot map records the details.
  if (kernel_init() != ESP_OK) {
    ESP_LOGW("MAIN", "Recovery boot: leaving pending OTA unconfirmed and assets unchanged");
    console_service_start();
    return;
  }

  // Confirm a pending OTA image AFTER the system is up, using local health
  // (assets mounted + LVGL alive) - never an optional peripheral like the C5.
  // A failed check leaves the image unconfirmed so the bootloader rolls back.
  ota_post_boot_check();
  console_service_start();
}
