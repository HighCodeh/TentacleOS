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


#define FLIPPER_SUBGHZ_PLATFORM_IMPL
#include "flipper_subghz.h"
#include "flipper_subghz_compat.h"
#include "lib/subghz/subghz_keystore.h"
#include "lib/subghz/subghz_keystore_i.h"
#include <assert.h>

#define TEST_KEY UINT64_C(0x0123456789ABCDEF)
#define TEST_AES_SIZE 16U

static esp_err_t verify_platform(void *context) {
  const char *path = context;
  SubGhzKeystore *store = subghz_keystore_alloc();
  assert(subghz_keystore_load(store, path));
  assert(store->data[0].count == 2);
  assert(store->data[0].items[0].key == TEST_KEY);
  assert(store->data[0].items[0].type == 1);
  assert(strcmp(furi_string_get_cstr(store->data[0].items[0].name), "Fixture") == 0);
  assert(store->data[0].items[1].key == TEST_KEY);
  static const uint8_t key[TEST_AES_SIZE] = {0};
  static const uint8_t plain[TEST_AES_SIZE] = {0};
  static const uint8_t expected[TEST_AES_SIZE] = {
    0x66,0xe9,0x4b,0xd4,0xef,0x8a,0x2c,0x3b,0x88,0x4c,0xfa,0x59,0xca,0x34,0x2b,0x2e};
  uint8_t cipher[TEST_AES_SIZE], recovered[TEST_AES_SIZE];
  assert(furi_hal_crypto_aes128_ecb_encrypt(key, plain, cipher));
  assert(memcmp(cipher, expected, TEST_AES_SIZE) == 0);
  assert(furi_hal_crypto_aes128_ecb_decrypt(key, cipher, recovered));
  assert(memcmp(plain, recovered, TEST_AES_SIZE) == 0);
  return ESP_OK;
}

static esp_err_t exhaust_arena(void *context) {
  (void)context;
  flipper_subghz_malloc(SIZE_MAX);
  return ESP_OK;
}

void test_flipper_platform(const char *path) {
  FILE *file = fopen(path, "w");
  assert(file != NULL);
  assert(fputs("Manufacturer: Fixture\nKey (Hex): 0123456789ABCDEF\nType: 1\n"
               "0123456789ABCDEF:2:Fixture2\n", file) >= 0);
  assert(fclose(file) == 0);
  assert(flipper_subghz_guard(verify_platform, (void *)path) == ESP_OK);
  assert(flipper_subghz_guard(exhaust_arena, NULL) == ESP_ERR_NO_MEM);
  assert(flipper_subghz_guard(verify_platform, (void *)path) == ESP_OK);
  assert(remove(path) == 0);
}
