#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "boot_report.h"
#include "kernel.h"
#include "ota_service.h"
#include "ui_manager.h"

void app_main(void) {
  // First: update the consecutive-abnormal-boot counter from the reset reason.
  // kernel_init drops into safe mode when this reports a boot loop.
  boot_report_track_bootloop();

  ota_post_boot_check();

  // kernel_init aborts to safe mode on a required-subsystem failure or a boot
  // loop, and reports it via the return code; the boot map records the details.
  if (kernel_init() != ESP_OK) {
    ESP_LOGE("MAIN", "Boot degraded: a required subsystem failed, running in safe mode");
  }
}
