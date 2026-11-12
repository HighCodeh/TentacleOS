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
#include <string.h>

#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"

typedef int esp_err_t;
typedef int hb_nfc_err_t;
typedef enum { MF_KEY_A, MF_KEY_B } mf_key_type_t;
typedef struct { uint8_t main, timer, error, target, collision; } st25r3916_irq_status_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -3
#define HB_NFC_ERR_INTERNAL -4
#define HB_NFC_ERR_TX_TIMEOUT -5
#define RF_FRONTEND_NFC_13MHZ 2
#define CHECK(condition) do { assert(condition); s_checks++; } while (0)
/* CONSTANTS */
static unsigned s_checks, s_calls, s_fail_at, s_irq_calls;
static uint8_t s_regs[256];
static size_t s_loaded;
static bool s_no_txe, s_initialized = true, s_access = true;
static bool s_incomplete, s_framing_error;
static int64_t s_now;
static int64_t esp_timer_get_time(void) { return s_now += 100; }
static bool hb_nfc_gpio_irq_level(void) { return true; }
static st25r3916_irq_status_t s_pending_irq;
static int operation(void) { return ++s_calls == s_fail_at ? -99 : ESP_OK; }
static bool rf_frontend_can_access(int mode) { assert(mode == 2); return s_access; }
static esp_err_t hb_nfc_spi_direct_cmd(uint8_t cmd) {
  assert(cmd == ST25R3916_CMD_TX_WO_CRC || cmd == ST25R3916_CMD_CLEAR_FIFO ||
         cmd == ST25R3916_CMD_MEAS_AMPLITUDE);
  return operation();
}
static esp_err_t hb_nfc_spi_reg_write(uint8_t reg, uint8_t value) {
  int err = operation();
  if (!err) s_regs[reg] = value;
  return err;
}
static esp_err_t hb_nfc_spi_reg_read(uint8_t reg, uint8_t *out) {
  int err = operation();
  *out = err ? 0 : s_regs[reg];
  return err;
}
static esp_err_t st25r3916_fifo_load(const uint8_t *data, size_t len) {
  assert(data != NULL);
  s_loaded = len;
  return operation();
}
static esp_err_t st25r3916_fifo_read(uint8_t *data, size_t len) {
  memset(data, 0xA5, len);
  return operation();
}
static esp_err_t st25r3916_irq_read(st25r3916_irq_status_t *out) {
  memset(out, 0, sizeof(*out));
  ++s_irq_calls;
  if (s_irq_calls == 2) {
    out->main = ST25R3916_IRQ_MAIN_RXE | (s_no_txe ? 0 : ST25R3916_IRQ_MAIN_TXE);
    out->timer = 8;
    out->error = 0x80;
    out->target = 1;
    if (s_incomplete) out->main = ST25R3916_IRQ_MAIN_RXS;
    if (s_framing_error) out->error |= 0x10;
  }
  return operation();
}
static void hb_nfc_timer_delay_us(uint32_t us) { (void)us; }
static void hb_nfc_timer_delay_ms(uint32_t ms) { (void)ms; }
/* PRODUCTION */

static void reset_bus(void) {
  s_calls = s_fail_at = s_irq_calls = 0;
  s_loaded = 0;
  s_no_txe = false;
  s_incomplete = s_framing_error = false;
  s_now = 0;
  memset(s_regs, 0, sizeof(s_regs));
  memset(&s_pending_irq, 0, sizeof(s_pending_irq));
}

static void set_access(uint8_t trailer[16], unsigned groups) {
  memset(trailer, 0, 16);
  for (unsigned g = 0; g < 4; g++) {
    unsigned ac = (groups >> (g * 3)) & 7;
    unsigned c1 = ac >> 2, c2 = (ac >> 1) & 1, c3 = ac & 1;
    trailer[6] |= ((c1 ^ 1) << g) | ((c2 ^ 1) << (4 + g));
    trailer[7] |= (c1 << (4 + g)) | ((c3 ^ 1) << g);
    trailer[8] |= (c2 << g) | (c3 << (4 + g));
  }
}

static void test_acl(void) {
  // Independent NXP MF1S50YYX/V1 tables 7 and 8. Bits: A=1, B=2.
  const unsigned read_mask[8] = {3, 3, 3, 2, 3, 2, 3, 0};
  const unsigned write_mask[8] = {3, 0, 0, 2, 2, 0, 2, 0};
  const unsigned inc_mask[8] = {3, 0, 0, 0, 0, 0, 2, 0};
  const unsigned dec_mask[8] = {3, 3, 0, 0, 0, 0, 3, 0};
  const unsigned trailer_keys[8] = {1, 1, 0, 2, 2, 0, 0, 0};
  const unsigned trailer_access[8] = {0, 1, 0, 2, 0, 2, 0, 0};
  for (unsigned groups = 0; groups < 4096; groups++) {
    uint8_t trailer[16], a, b, c;
    set_access(trailer, groups);
    unsigned trailer_ac = groups >> 9;
    for (unsigned g = 0; g < 4; g++) {
      unsigned ac = (groups >> (g * 3)) & 7;
      CHECK(mfc_emu_get_access_bits(trailer, g, &a, &b, &c));
      CHECK(((a << 2) | (b << 1) | c) == (int)ac);
      for (unsigned key = 0; key < 2; key++) {
        bool allowed = key == 0 || (trailer_ac != 0 && trailer_ac != 1 && trailer_ac != 2);
        unsigned mask = 1U << key;
        bool can_read = g == 3 || (read_mask[ac] & mask);
        bool can_write = g == 3 ? ((trailer_keys[ac] | trailer_access[ac]) & mask)
                               : (write_mask[ac] & mask);
        CHECK(mfc_emu_can_read(trailer, g, key) == (allowed && can_read));
        CHECK(mfc_emu_can_write(trailer, g, key) == (allowed && can_write));
        CHECK(mfc_emu_can_increment(trailer, g, key) ==
              (allowed && g != 3 && (inc_mask[ac] & mask) != 0));
        CHECK(mfc_emu_can_decrement(trailer, g, key) ==
              (allowed && g != 3 && (dec_mask[ac] & mask) != 0));
      }
    }
    for (unsigned bit = 0; bit < 24; bit++) {
      trailer[6 + bit / 8] ^= 1U << (bit % 8);
      CHECK(!mfc_emu_get_access_bits(trailer, 0, &a, &b, &c));
      CHECK(!mfc_emu_can_read(trailer, 3, MF_KEY_A));
      trailer[6 + bit / 8] ^= 1U << (bit % 8);
    }
  }
  for (unsigned block = 0; block < 256; block++) {
    unsigned expected = block < 128 ? block % 4 : (block % 16) / 5;
    CHECK(block_index_in_sector(block) == (int)expected);
  }
  uint8_t trailer[16], a, b, c;
  set_access(trailer, 0);
  CHECK(!mfc_emu_get_access_bits(NULL, 0, &a, &b, &c));
  CHECK(!mfc_emu_get_access_bits(trailer, -1, &a, &b, &c));
  CHECK(!mfc_emu_get_access_bits(trailer, 4, &a, &b, &c));
  CHECK(!mfc_emu_get_access_bits(trailer, 0, NULL, &b, &c));
  CHECK(!mfc_emu_can_read(trailer, 0, (mf_key_type_t)2));
}

static void test_transport(void) {
  uint8_t data[32] = {0};
  for (unsigned len = 1; len <= MFC_TX_MAX_BYTES; len++) {
    for (unsigned tail = 0; tail < 8; tail++) {
      reset_bus();
      CHECK(target_tx_raw(data, len, tail) == HB_NFC_OK);
      unsigned bits = (s_regs[ST25R3916_REG_NUM_TX_BYTES1] << 8) |
                      s_regs[ST25R3916_REG_NUM_TX_BYTES2];
      CHECK(bits == (len - (tail != 0)) * 8 + tail);
      CHECK(s_loaded == len);
      st25r3916_irq_status_t next;
      CHECK(read_irq(&next) == ESP_OK);
      CHECK(next.main == ST25R3916_IRQ_MAIN_RXE && next.error == 0x80 && next.timer == 8);
      CHECK(read_irq(&next) == ESP_OK && next.main == 0 && next.error == 0);
    }
  }
  reset_bus();
  CHECK(target_tx_raw(data, 5, 4) == HB_NFC_OK);
  unsigned calls = s_calls;
  for (unsigned fault = 1; fault <= calls; fault++) {
    reset_bus(); s_fail_at = fault;
    CHECK(target_tx_raw(data, 5, 4) == HB_NFC_ERR_INTERNAL);
  }
  reset_bus(); s_no_txe = true;
  CHECK(target_tx_raw(data, 1, 4) == HB_NFC_ERR_TX_TIMEOUT);
  reset_bus();
  CHECK(target_tx_raw(NULL, 4, 0) == HB_NFC_ERR_PARAM && s_calls == 0);
  CHECK(target_tx_raw(data, 0, 0) == HB_NFC_ERR_PARAM && s_calls == 0);
  CHECK(target_tx_raw(data, 25, 0) == HB_NFC_ERR_PARAM && s_calls == 0);
  CHECK(target_tx_raw(data, 4, 8) == HB_NFC_ERR_PARAM && s_calls == 0);
  for (unsigned count = 0; count < 1024; count++) {
    reset_bus();
    s_regs[ST25R3916_REG_FIFO_STATUS1] = count & 255;
    s_regs[ST25R3916_REG_FIFO_STATUS2] = (count >> 8) << 6;
    CHECK(fifo_rx(data, sizeof(data)) == (count <= sizeof(data) ? (int)count : -1));
  }
  for (unsigned fault = 1; fault <= 3; fault++) {
    reset_bus(); s_fail_at = fault;
    s_regs[ST25R3916_REG_FIFO_STATUS1] = 4;
    CHECK(fifo_rx(data, sizeof(data)) == -1);
  }
  for (unsigned flag = 0x10; flag <= 0x30; flag += 0x10) {
    reset_bus(); s_regs[ST25R3916_REG_FIFO_STATUS2] = flag;
    CHECK(fifo_rx(data, sizeof(data)) == -1);
  }
}

static void test_measurement(void) {
  uint8_t value;
  reset_bus();
  CHECK(highboy_nfc_measure_amplitude_checked(NULL) == ESP_ERR_INVALID_ARG && s_calls == 0);
  for (unsigned raw = 0; raw < 256; raw++) {
    reset_bus(); s_regs[ST25R3916_REG_AD_RESULT] = raw;
    CHECK(highboy_nfc_measure_amplitude_checked(&value) == ESP_OK && value == raw);
  }
  for (unsigned fault = 1; fault <= 2; fault++) {
    reset_bus(); s_fail_at = fault; value = 123;
    CHECK(highboy_nfc_measure_amplitude_checked(&value) != ESP_OK && value == 0);
  }
  reset_bus(); s_access = false; value = 123;
  CHECK(highboy_nfc_measure_amplitude_checked(&value) == ESP_ERR_INVALID_STATE && value == 0 && s_calls == 0);
  s_access = true; s_initialized = false;
  CHECK(highboy_nfc_measure_amplitude_checked(&value) == ESP_ERR_INVALID_STATE && s_calls == 0);
}

static void test_auth_receive(void) {
  uint8_t data[8];
  reset_bus(); s_regs[ST25R3916_REG_FIFO_STATUS1] = 8;
  CHECK(target_rx_irq(data, sizeof(data), 50) == 8);
  CHECK(s_now < 50000);
  unsigned operations = s_calls;
  for (unsigned fail = 1; fail <= operations; fail++) {
    reset_bus(); s_regs[ST25R3916_REG_FIFO_STATUS1] = 8; s_fail_at = fail;
    CHECK(target_rx_irq(data, sizeof(data), 50) == -1);
  }
  reset_bus(); s_regs[ST25R3916_REG_FIFO_STATUS1] = 8; s_incomplete = true;
  CHECK(target_rx_irq(data, sizeof(data), 50) == 0);
  CHECK(s_now >= 50000 && s_now <= 50200);
  reset_bus(); s_regs[ST25R3916_REG_FIFO_STATUS1] = 8; s_framing_error = true;
  CHECK(target_rx_irq(data, sizeof(data), 50) == -1);
  reset_bus(); s_regs[ST25R3916_REG_FIFO_STATUS1] = 9;
  CHECK(target_rx_irq(data, sizeof(data), 50) == -1);
}

int main(void) {
  test_acl();
  test_transport();
  test_measurement();
  test_auth_receive();
  printf("PASS: %u checks: Classic ACL, 4K groups, TX lengths/IRQ/faults, FIFO, RF sampling.\n", s_checks);
  puts("Host helpers only; custom-parity target PHY and RF interoperability NOT validated.");
  return 0;
}
