#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kernel.h"


void app_main(void) {
  kernel_init();
}
