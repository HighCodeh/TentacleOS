#include "console_service.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static int cmd_free(int argc, char **argv) {
    printf("Internal RAM:\n");
    printf("  Free: %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("  Min Free: %lu bytes\n", (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    
    printf("SPIRAM (PSRAM):\n");
    printf("  Free: %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("  Min Free: %lu bytes\n", (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int cmd_restart(int argc, char **argv) {
    printf("Restarting system...\n");
    esp_restart();
    return 0;
}

void register_system_commands(void) {
    const esp_console_cmd_t cmd_free_def = {
        .command = "free",
        .help = "Show remaining memory",
        .hint = NULL,
        .func = &cmd_free,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_free_def));

    const esp_console_cmd_t cmd_restart_def = {
        .command = "restart",
        .help = "Reboot the Highboy",
        .hint = NULL,
        .func = &cmd_restart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_restart_def));
}
