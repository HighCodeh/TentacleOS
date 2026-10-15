/*
 * Entry point for the littlefs test application (ESP-IDF v6.0+)
 */

#include "unity.h"

void app_main(void)
{
    printf("Running littlefs tests\n");
    unity_run_menu();
}
