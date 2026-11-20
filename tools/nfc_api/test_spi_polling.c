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
#include <stddef.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define SPI3_HOST 3
#define RF_FRONTEND_NFC_13MHZ 1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef struct { int length; } spi_transaction_t;
static void *s_spi = (void *)1;
static int s_spi_host = SPI3_HOST;
static bool locked, reject_lock, reject_owner, revoke_on_lock;
static int transfers, transfer_error;
static bool rf_frontend_can_access(int owner) { (void)owner; return !reject_owner; }
static bool spi_bus_lock_take(int timeout) {
  assert(timeout > 0 && !locked);
  if (reject_lock) return false;
  locked = true;
  if (revoke_on_lock) reject_owner = true;
  return true;
}
static void spi_bus_lock_give(void) { assert(locked); locked = false; }
static int spi_device_polling_transmit(void *device, spi_transaction_t *transaction) {
  assert(device == s_spi && transaction && !reject_owner);
  assert(s_spi_host != SPI3_HOST || locked);
  transfers++;
  return transfer_error;
}
/* PRODUCTION */
int main(void) {
  spi_transaction_t transaction = {.length = 16};
  assert(transmit(&transaction) == ESP_OK && transfers == 1 && !locked);
  transfer_error = ESP_FAIL;
  assert(transmit(&transaction) == ESP_FAIL && !locked);
  transfer_error = 0;
  reject_lock = true;
  assert(transmit(&transaction) == ESP_ERR_TIMEOUT && !locked && transfers == 2);
  reject_lock = false;
  revoke_on_lock = true;
  assert(transmit(&transaction) == ESP_ERR_INVALID_STATE && !locked && transfers == 2);
  revoke_on_lock = false;
  assert(transmit(&transaction) == ESP_ERR_INVALID_STATE && !locked);
  reject_owner = false;
  assert(transmit(NULL) == ESP_FAIL && !locked);
  s_spi_host = 2;
  assert(transmit(&transaction) == ESP_OK && !locked && transfers == 3);
  puts("NFC polling SPI ownership and failure cleanup passed");
}
