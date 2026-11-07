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
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "st25r3916_reg.h"
#include "st25r3916_irq.h"
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_TIMEOUT 3
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)snprintf(NULL, 0, __VA_ARGS__))
#define ESP_LOGD(...) ((void)0)
static int64_t now;
static uint8_t regs[256];
static uint8_t s_pending_main, s_pending_error;
static int fail_reg = -1;
static int64_t esp_timer_get_time(void) { return now; }
static void hb_nfc_timer_delay_us(int us) { now += us; }
static int hb_nfc_gpio_irq_level(void) { return 0; }
static int hb_nfc_spi_reg_read(int reg, uint8_t *out) {
  now += 1000;
  if (reg == fail_reg) return ESP_FAIL;
  *out = regs[reg];
  regs[reg] = 0;
  return ESP_OK;
}
static uint16_t st25r3916_fifo_get_count(void) { now += 2000; return 0; }
/* PRODUCTION */
int main(void) {
  st25r3916_irq_status_t status;
  regs[ST25R3916_REG_MAIN_INT] = ST25R3916_IRQ_MAIN_TXE | ST25R3916_IRQ_MAIN_RXE;
  regs[ST25R3916_REG_ERROR_INT] = 0x80;
  assert(st25r3916_irq_wait_txe() == ESP_OK);
  assert(st25r3916_irq_wait_rxe(20, &status) == ESP_OK);
  assert(status.main & ST25R3916_IRQ_MAIN_RXE);
  assert(status.error == 0x80);
  assert(st25r3916_irq_read(&status) == ESP_OK);
  assert(status.main == 0 && status.error == 0);
  s_pending_main = ST25R3916_IRQ_MAIN_RXE;
  assert(st25r3916_irq_clear() == ESP_OK);
  now = 0;
  assert(st25r3916_irq_wait_rxe(20, &status) == ESP_ERR_TIMEOUT);
  assert(now >= 20000 && now < 26000);
  now = 0;
  uint16_t count = 9;
  assert(st25r3916_fifo_wait(2, 20, &count) == ESP_ERR_TIMEOUT);
  assert(count == 0 && now >= 20000 && now < 23000);
  now = 0;
  assert(st25r3916_irq_wait_txe() == ESP_ERR_TIMEOUT);
  assert(now >= 20000 && now < 26000);
  fail_reg = ST25R3916_REG_MAIN_INT;
  assert(st25r3916_irq_read(&status) == ESP_FAIL);
  puts("IRQ handoff and elapsed-time waits passed");
}
