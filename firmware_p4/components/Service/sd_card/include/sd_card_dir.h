#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SD_BASE_PATH "/sdcard"

// Diretórios predefinidos do sistema
#define SD_DIR_IR       "/ir"
#define SD_DIR_BADUSB   "/badusb"
#define SD_DIR_NFC      "/nfc"
#define SD_DIR_RFID     "/rfid"
#define SD_DIR_SUBGHZ   "/subghz"
#define SD_DIR_CONFIG   "/config"
#define SD_DIR_LOGS     "/logs"
#define SD_DIR_BACKUP   "/backups"

typedef void (*sd_dir_callback_t)(const char *name, bool is_dir, void *user_data);

esp_err_t sd_dir_create(const char *path);
esp_err_t sd_dir_remove_recursive(const char *path);
bool      sd_dir_exists(const char *path);
esp_err_t sd_dir_list(const char *path, sd_dir_callback_t callback, void *user_data);
esp_err_t sd_dir_count(const char *path, uint32_t *file_count, uint32_t *dir_count);
esp_err_t sd_dir_copy_recursive(const char *src, const char *dst);
esp_err_t sd_dir_get_size(const char *path, uint64_t *total_size);