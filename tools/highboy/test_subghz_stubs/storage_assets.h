#ifndef TEST_SUBGHZ_STORAGE_ASSETS_H
#define TEST_SUBGHZ_STORAGE_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t storage_assets_get_file_size(const char *path, size_t *out_size);
esp_err_t storage_assets_read_file(const char *path, uint8_t *out, size_t size, size_t *out_read);
#endif
