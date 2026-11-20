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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "st25r3916_reg.h"
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_LOGW(...) ((void)0)
#define RF_FRONTEND_NFC_13MHZ 1
#define SPI_TRANS_USE_TXDATA 1
#define SPI_TRANS_USE_RXDATA 2
#define SPI_TRANS_VARIABLE_CMD 4
typedef int esp_err_t;
typedef struct { uint32_t flags, length; uint16_t cmd; uint8_t tx_data[4], rx_data[4]; } spi_transaction_t;
typedef struct { spi_transaction_t base; uint8_t command_bits; } spi_transaction_ext_t;
static uint8_t regs[64], last_command;
static unsigned operations, releases;
static int failure, claim_failure;
static int rf_frontend_claim(int owner) { assert(owner==RF_FRONTEND_NFC_13MHZ); return claim_failure; }
static void rf_frontend_release(int owner) { assert(owner==RF_FRONTEND_NFC_13MHZ); releases++; }
/* CONSTANTS */
static int transmit(spi_transaction_t *t) {
  operations++;
  assert(t->flags & SPI_TRANS_USE_TXDATA);
  if (failure) return failure;
  if (t->flags & SPI_TRANS_VARIABLE_CMD) {
    assert(((spi_transaction_ext_t *)t)->command_bits==8);
    assert((t->flags & SPI_TRANS_USE_RXDATA) && (t->cmd & HB_NFC_SPI_READ_FLAG));
    assert(t->length>=8 && t->length<=32 && t->length%8==0);
    for (unsigned i=0;i<t->length/8;i++) t->rx_data[i]=regs[(t->cmd & HB_NFC_SPI_ADDR_MASK)+i];
  } else if (t->flags & SPI_TRANS_USE_RXDATA) {
    assert(t->length==16 && (t->tx_data[0] & HB_NFC_SPI_READ_FLAG) && t->tx_data[1]==0);
    t->rx_data[1]=regs[t->tx_data[0] & HB_NFC_SPI_ADDR_MASK];
  } else if (t->length==16) {
    assert(t->tx_data[0]<=HB_NFC_SPI_ADDR_MASK);
    regs[t->tx_data[0]]=t->tx_data[1];
  } else {
    assert(t->length==8); last_command=t->tx_data[0];
  }
  return ESP_OK;
}
/* PRODUCTION */
int main(void) {
  for (unsigned i=0;i<sizeof(regs);i++) regs[i]=(uint8_t)(i^0xa5);
  uint8_t out[4]={0}, single=0;
  for (size_t n=1;n<=4;n++) {
    assert(hb_nfc_spi_reg_read_many(0x1a,out,n)==ESP_OK);
    assert(!memcmp(out,regs+0x1a,n));
  }
  assert(hb_nfc_spi_reg_read(0x1a,&single)==ESP_OK && single==regs[0x1a]);
  assert(hb_nfc_spi_reg_write(0x09,0x72)==ESP_OK && regs[0x09]==0x72);
  assert(hb_nfc_spi_direct_cmd(0xdc)==ESP_OK && last_command==0xdc);
  unsigned before=operations;
  assert(hb_nfc_spi_reg_read_many(0,out,0)==ESP_ERR_INVALID_ARG);
  assert(hb_nfc_spi_reg_read_many(0,out,5)==ESP_ERR_INVALID_ARG);
  assert(hb_nfc_spi_reg_read_many(64,out,1)==ESP_ERR_INVALID_ARG);
  assert(hb_nfc_spi_reg_read_many(63,out,2)==ESP_ERR_INVALID_ARG);
  assert(hb_nfc_spi_reg_read_many(0,NULL,1)==ESP_ERR_INVALID_ARG);
  assert(operations==before);
  failure=9; memset(out,0xff,sizeof(out));
  assert(hb_nfc_spi_reg_read_many(0x1a,out,4)==9);
  assert(!memcmp(out,(uint8_t[4]){0},sizeof(out)));
  assert(hb_nfc_spi_reg_read(0x1a,&single)==9 && single==0);
  failure=0; claim_failure=7; before=operations;
  assert(hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL,ST25R3916_OP_CTRL_TX_EN)==7);
  assert(operations==before && releases==0);
  claim_failure=0;
  assert(hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL,0)==ESP_OK && releases==1);
  puts("PASS: inline SPI bytes, burst command phase, bounds, failures and RF ownership");
}
