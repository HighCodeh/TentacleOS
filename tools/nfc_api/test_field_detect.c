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
#include "st25r3916_reg.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define RF_FRONTEND_NFC_13MHZ 1
static bool s_initialized, access_allowed;
static unsigned operations, failure_at, waits;
static uint8_t op_register, aux_register;
/* CONSTANTS */
static bool rf_frontend_can_access(int mode) { assert(mode==1); return access_allowed; }
static int operation(void) { return ++operations==failure_at ? 77 : ESP_OK; }
static esp_err_t st25r3916_core_field_off(void) {
 int e=operation(); if (!e) op_register=ST25R3916_OP_CTRL_FIELD_OFF; return e;
}
static esp_err_t hb_nfc_spi_reg_write(uint8_t addr,uint8_t value) {
 assert(addr==ST25R3916_REG_OP_CTRL); assert(!(value&ST25R3916_OP_CTRL_TX_EN));
 assert(value==(ST25R3916_OP_CTRL_EN|2)); int e=operation(); if (!e)op_register=value; return e;
}
static void hb_nfc_timer_delay_ms(unsigned ms) {
 assert(ms==5); assert(op_register==(ST25R3916_OP_CTRL_EN|2)); waits++;
}
static esp_err_t hb_nfc_spi_reg_read(uint8_t addr,uint8_t *out) {
 assert(addr==ST25R3916_REG_AUX_DISPLAY); assert(op_register==(ST25R3916_OP_CTRL_EN|2));
 int e=operation(); if (!e)*out=aux_register; return e;
}
/* PRODUCTION */
static void reset(void) {
 s_initialized=access_allowed=true; operations=failure_at=waits=0;
 op_register=0; aux_register=ST25R3916_AUX_DISP_OSC_OK|ST25R3916_AUX_DISP_EFD_BIT;
}
int main(void) {
 bool detected=true; uint8_t aux=255;
 reset(); assert(highboy_nfc_detect_external_field(&detected,&aux)==ESP_OK);
 assert(detected && aux==aux_register && operations==4 && waits==1 && op_register==0x80);
 reset(); aux_register=ST25R3916_AUX_DISP_OSC_OK;
 assert(highboy_nfc_detect_external_field(&detected,&aux)==ESP_OK);
 assert(!detected && aux==aux_register && op_register==0x80);
 reset(); assert(highboy_nfc_detect_external_field(&detected,NULL)==ESP_OK && detected);
 for (unsigned n=1;n<=4;n++) {
  reset(); failure_at=n; detected=true; aux=255;
  assert(highboy_nfc_detect_external_field(&detected,&aux)==77);
  assert(!detected && aux==0);
  if (n==2 || n==3)assert(op_register==0x80);
 }
 reset(); s_initialized=false;
 assert(highboy_nfc_detect_external_field(&detected,&aux)==ESP_ERR_INVALID_STATE && operations==0);
 reset(); access_allowed=false;
 assert(highboy_nfc_detect_external_field(&detected,&aux)==ESP_ERR_INVALID_STATE && operations==0);
 reset(); assert(highboy_nfc_detect_external_field(NULL,&aux)==ESP_ERR_INVALID_ARG && operations==0);
 puts("PASS: field detector enabled before sampling, TX remains off, cleanup/failure and ownership checks");
}
