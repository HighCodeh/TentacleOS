#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kernel.h"
#include "ota_service.h"
#include "ui_manager.h"


void app_main(void) {
  ota_post_boot_check();
  kernel_init();
}
