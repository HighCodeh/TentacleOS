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
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
#define ST25R3916_IRQ_MAIN_RXE 0x10
#define ESP_LOGW(...) ((void)0)
#define ST25R3916_CMD_STOP_ALL 0
#define ST25R3916_CMD_RESET_RX_GAIN 1
#define ST25R3916_CMD_CLEAR_FIFO 2
#define ST25R3916_CMD_TX_WITH_CRC 3
#define ST25R3916_CMD_TX_WO_CRC 4
#define ST25R3916_REG_NUM_TX_BYTES1 0
#define ST25R3916_REG_NUM_TX_BYTES2 1
#define ST25R3916_REG_FIFO_STATUS1 2
#define ST25R3916_REG_FIFO_STATUS2 3
#define ST25R3916_FIFO_TX_BYTES_LSB_MASK 31
#define ST25R3916_FIFO_STATUS2_MSB_MASK 0xC0
#define ST25R3916_IRQ_MAIN_COL 4
#define ST25R3916_IRQ_MAIN_TXE 0x08
#define NFC_POLLER_TIMEOUT_MAX_MS 5000
#define NFC_POLLER_FIFO_CAPACITY 512
#define NFC_POLLER_FIFO_ERROR_MASK 0x30
#define NFC_POLLER_PARTIAL_BYTE_MASK 0x0E
#define NFC_POLLER_CRC_ERROR 0x80
#define NFC_POLLER_CLASSIC_NONCE_BYTES 4
#define NFC_POLLER_CLASSIC_AUTH_BYTES 2
#define NFC_POLLER_CLASSIC_KEY_A 0x60
#define NFC_POLLER_CLASSIC_KEY_B 0x61
#define esp_err_to_name(x) "error"
typedef int esp_err_t;
typedef struct { uint8_t main,error,timer,target,collision; } st25r3916_irq_status_t;
static uint32_t s_guard_time_us,s_fdt_min_us,s_last_fdt_us;
static bool s_validate_fdt;
static uint8_t error,count=4,flags,main_irq=0x30;
static int wait_result;
static int64_t esp_timer_get_time(void) { return 100; }
static void hb_nfc_timer_delay_us(uint32_t us) { (void)us; }
static int hb_nfc_spi_direct_cmd(int cmd) { (void)cmd; return 0; }
static int hb_nfc_spi_reg_write(int reg,uint8_t value) { (void)reg;(void)value;return 0; }
static int hb_nfc_spi_reg_read(int reg,uint8_t *value) { *value=reg==2?count:flags;return 0; }
static int st25r3916_irq_clear(void) { return 0; }
static int st25r3916_irq_wait_exchange(int timeout,st25r3916_irq_status_t *irq) {
 (void)timeout; irq->error=error;irq->main=main_irq;return wait_result;
}
static int st25r3916_fifo_load(const uint8_t *data,size_t size) { (void)data;(void)size;return 0; }
static int st25r3916_fifo_read(uint8_t *data,size_t size) { memset(data,0xA5,size);return 0; }
/* PRODUCTION */
int main(void) {
 uint8_t auth[]={0x60,0},out[4];
 for(unsigned e=0;e<256;e++) {
  error=e;
  assert((nfc_poller_classic_nonce(auth,out,20)==4)==(e==0||e==0x80));
  assert((nfc_poller_transceive(auth,2,true,out,4,4,20)==4)==(e==0));
 }
 error=0x80;
 for(unsigned n=0;n<8;n++) { count=n;assert((nfc_poller_classic_nonce(auth,out,20)==4)==(n==4)); }
 count=4;wait_result=1;assert(nfc_poller_classic_nonce(auth,out,20)==0);
 wait_result=0;flags=0x10;assert(nfc_poller_classic_nonce(auth,out,20)==0);
 flags=2;assert(nfc_poller_classic_nonce(auth,out,20)==0);
 flags=0;main_irq=4;assert(nfc_poller_classic_nonce(auth,out,20)==0);
 main_irq=0x30;auth[0]=0x30;assert(nfc_poller_classic_nonce(auth,out,20)==0);
 puts("PASS: CRC-less nonce exception remains scoped; parity/framing/length/timeout rejected");
 error=0;flags=0;main_irq=0x08;count=0;wait_result=ESP_ERR_TIMEOUT;
 assert(nfc_poller_expect_silence(auth,2,5));
 wait_result=1;assert(!nfc_poller_expect_silence(auth,2,5));
 wait_result=ESP_ERR_TIMEOUT;count=1;assert(!nfc_poller_expect_silence(auth,2,5));
 count=0;error=0x80;assert(!nfc_poller_expect_silence(auth,2,5));
 error=0;main_irq=0x10;assert(!nfc_poller_expect_silence(auth,2,5));
 main_irq=0;assert(!nfc_poller_expect_silence(auth,2,5));
 assert(!nfc_poller_expect_silence(NULL,2,5));
}
