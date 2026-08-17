#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *command;
    const char *help;
    const char *hint;
    int (*func)(int argc, char **argv);
    void *argtable;
} esp_console_cmd_t;

esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);
esp_err_t esp_console_init(const void *config);
esp_err_t esp_console_start(void);

#ifdef __cplusplus
}
#endif
