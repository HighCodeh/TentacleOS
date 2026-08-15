#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kernel.h"
#include "ota_service.h"

void app_main(void) {
  kernel_init();
  ota_post_boot_check();
}
