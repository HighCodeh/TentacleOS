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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int esp_err_t;
typedef int nvs_handle_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG,
       ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_SIZE };
enum { SPI_CFG_WIFI_AP, SPI_CFG_BLE_ANNOUNCE, SPI_CFG_BLE_SPAM,
       SPI_CFG_CHAT, SPI_CFG_CHAT_ADDRS, SPI_CFG_GROK, SPI_CFG_COUNT };
enum { NVS_READONLY, NVS_READWRITE, MALLOC_CAP_SPIRAM = 4, MALLOC_CAP_8BIT = 8 };
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
static bool s_fail_alloc;
static int s_write_result;
static unsigned s_allocations, s_writes;
static uint32_t s_saved_hash;
static char s_written[4097];
static void *heap_caps_calloc(size_t n, size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ++s_allocations;
  return s_fail_alloc ? NULL : calloc(n, size);
}
static void mbedtls_platform_zeroize(void *data, size_t size) { memset(data, 0, size); }
static int nvs_open(const char *name, int mode, nvs_handle_t *out) {
  (void)name; (void)mode; *out = 1; return ESP_OK;
}
static int nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *out) {
  (void)h; (void)key; *out = s_saved_hash; return ESP_OK;
}
static int nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value) {
  (void)h; (void)key; s_saved_hash = value; return ESP_OK;
}
static int nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static void nvs_close(nvs_handle_t h) { (void)h; }
static int storage_assets_write_file(const char *path, const char *data) {
  assert(path != NULL); assert(strlen(data) < sizeof(s_written));
  strcpy(s_written, data); ++s_writes; return s_write_result;
}
/* PRODUCTION */
static void assert_cleared(void) {
  for (size_t i = 0; i < CFG_STAGE_MAX + 1; ++i) assert(s_stage[i] == 0);
}
int main(void) {
  const uint8_t payload[] = "test";
  assert(config_cache_chunk(SPI_CFG_WIFI_AP, 0, payload, 4) == ESP_ERR_INVALID_STATE);
  assert(config_cache_commit(SPI_CFG_WIFI_AP) == ESP_ERR_INVALID_STATE);
  s_fail_alloc = true;
  assert(config_cache_begin(SPI_CFG_WIFI_AP, 4, 7) == ESP_ERR_NO_MEM);
  assert(s_writes == 0 && s_stage == NULL);
  assert(config_cache_commit(SPI_CFG_WIFI_AP) == ESP_ERR_INVALID_STATE);
  s_fail_alloc = false;
  assert(config_cache_begin(SPI_CFG_WIFI_AP, 4, 7) == ESP_OK);
  assert(config_cache_chunk(SPI_CFG_WIFI_AP, UINT32_MAX, payload, 4) == ESP_ERR_INVALID_SIZE);
  assert(config_cache_chunk(SPI_CFG_WIFI_AP, 0, payload, 4) == ESP_OK);
  assert(config_cache_commit(SPI_CFG_WIFI_AP) == ESP_OK);
  assert(strcmp(s_written, "test") == 0 && config_cache_get_hash(SPI_CFG_WIFI_AP) == 7);
  assert(config_cache_begin(SPI_CFG_GROK, CFG_STAGE_MAX, 8) == ESP_OK);
  memset(s_stage, 'x', CFG_STAGE_MAX);
  assert(config_cache_commit(SPI_CFG_GROK) == ESP_OK);
  assert(strlen(s_written) == CFG_STAGE_MAX); assert_cleared();
  assert(config_cache_begin(SPI_CFG_GROK, 4, 9) == ESP_OK);
  assert(config_cache_chunk(SPI_CFG_GROK, 0, payload, 4) == ESP_OK);
  s_write_result = ESP_FAIL;
  assert(config_cache_commit(SPI_CFG_GROK) == ESP_FAIL); assert_cleared();
  assert(s_saved_hash == 8);
  assert(config_cache_begin(SPI_CFG_GROK, 4, 9) == ESP_OK);
  assert(config_cache_chunk(SPI_CFG_GROK, 0, payload, 4) == ESP_OK);
  assert(config_cache_begin(SPI_CFG_WIFI_AP, CFG_STAGE_MAX + 1, 0) == ESP_ERR_INVALID_ARG);
  assert_cleared();
  assert(s_allocations == 2);
  free(s_stage);
  puts("PASS: config cache PSRAM failure/retry, bounded full-size staging, reuse and credential cleanup");
}
