// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_link_rf.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "highboy_nfc.h"
#include "lf_rfid.h"
#include "nfc_manager.h"
#include "power_manager.h"
#include "rf_frontend.h"
#include "spi_protocol.h"

static _Thread_local unsigned s_identity;
void *xTaskGetCurrentTaskHandle(void) { return &s_identity; }
static esp_err_t s_init_error, s_poll_error, s_off_error, s_pm_error, s_pm_release_error;
static bool s_is_bad_uid;
static unsigned s_locks, s_reads, s_stops;

esp_err_t power_manager_no_sleep_acquire(void) {
  if(s_pm_error!=ESP_OK) return s_pm_error;
  ++s_locks; return ESP_OK;
}
esp_err_t power_manager_no_sleep_release(void) {
  assert(s_locks==1);
  if(s_pm_release_error!=ESP_OK) return s_pm_release_error;
  --s_locks; return ESP_OK;
}
esp_err_t highboy_nfc_init(const highboy_nfc_config_t *config) {
  assert(config!=NULL && rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));
  return s_init_error;
}
esp_err_t highboy_nfc_field_off(void) {
  assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));
  ++s_stops;
  if(s_off_error!=ESP_OK) return s_off_error;
  rf_frontend_release(RF_FRONTEND_NFC_13MHZ); return ESP_OK;
}
esp_err_t nfc_manager_poll(nfc_manager_tech_t tech, hb_nfc_card_data_t *out_card) {
  assert(s_locks==1);
  assert(rf_frontend_claim(RF_FRONTEND_NFC_13MHZ)==ESP_OK);
  ++s_reads;
  if(s_poll_error!=ESP_OK) return s_poll_error;
  memset(out_card,0,sizeof(*out_card));
  switch(tech) {
  case NFC_MANAGER_TECH_A:
    out_card->protocol=HB_PROTO_ISO14443_3A;
    out_card->iso14443a.uid_len=s_is_bad_uid ? 11 : 7;
    memcpy(out_card->iso14443a.uid,"1234567",7);
    out_card->iso14443a.atqa[0]=0x44;
    out_card->iso14443a.sak=0x08;
    break;
  case NFC_MANAGER_TECH_B:
    out_card->protocol=HB_PROTO_ISO14443_3B;
    memcpy(out_card->data.iso14443b.pupi,"B123",4);
    break;
  case NFC_MANAGER_TECH_F:
    out_card->protocol=HB_PROTO_FELICA;
    memcpy(out_card->data.felica.idm,"F1234567",8);
    break;
  case NFC_MANAGER_TECH_V:
    out_card->protocol=HB_PROTO_ISO15693;
    memcpy(out_card->data.iso15693.uid,"V1234567",8);
    break;
  default: assert(false);
  }
  return ESP_OK;
}
esp_err_t lf_rfid_init(void) { return s_init_error; }
esp_err_t lf_rfid_field_start(uint32_t hz,uint16_t duty) {
  assert(hz==LF_RFID_NOMINAL_HZ && duty==LF_RFID_DEFAULT_DUTY_PERMILLE && s_locks==1);
  return rf_frontend_claim(RF_FRONTEND_LF_125KHZ);
}
void lf_rfid_field_stop(void) { ++s_stops; rf_frontend_release(RF_FRONTEND_LF_125KHZ); }
esp_err_t lf_rfid_read_em4100(uint32_t milliseconds,lf_rfid_em4100_t *out) {
  assert(milliseconds==500 && rf_frontend_get_mode()==RF_FRONTEND_LF_125KHZ);
  ++s_reads;
  if(s_poll_error!=ESP_OK) return s_poll_error;
  memcpy(out->bytes,"ABCDE",5); out->inverted=true; out->samples_used=0x1234;
  return ESP_OK;
}

int main(void) {
  assert(SPI_CAT_MUSE==0x11 && SPI_CAT_NFC==0x12 && SPI_CAT_RFID==0x13);
  uint8_t output[HOST_RF_RESPONSE_MAX+1], payload[2]={0};
  host_rf_request_t request={.command=SPI_ID_NFC_READ,.payload=payload,.length=1,
                            .out=output,.capacity=HOST_RF_RESPONSE_MAX};
  assert(host_rf_handle(NULL)==SPI_STATUS_INVALID_ARG);
  for(uint8_t tech=0;tech<NFC_MANAGER_TECH_COUNT;++tech) {
    payload[0]=tech; memset(output,0xAA,sizeof(output));
    assert(host_rf_handle(&request)==SPI_STATUS_OK);
    assert(request.out_length==17 && output[0]==1 && output[1]==tech && output[17]==0xAA);
    assert(output[3]==(tech==0 ? 7 : tech==1 ? 4 : 8));
    if(tech==0) assert(memcmp(output+4,"1234567",7)==0 && output[14]==0x44 && output[16]==0x08);
    else assert(output[14]==0 && output[15]==0 && output[16]==0);
    assert(s_locks==0 && rf_frontend_get_mode()==RF_FRONTEND_IDLE);
  }
  payload[0]=0;
  unsigned reads=s_reads;
  assert(rf_frontend_claim(RF_FRONTEND_NFC_13MHZ)==ESP_OK);
  assert(host_rf_handle(&request)==SPI_STATUS_BUSY && s_reads==reads);
  rf_frontend_release(RF_FRONTEND_NFC_13MHZ);
  request.capacity=16; assert(host_rf_handle(&request)==SPI_STATUS_INVALID_ARG); request.capacity=17;
  request.payload=NULL; assert(host_rf_handle(&request)==SPI_STATUS_INVALID_ARG); request.payload=payload;
  payload[0]=NFC_MANAGER_TECH_COUNT; assert(host_rf_handle(&request)==SPI_STATUS_INVALID_ARG); payload[0]=0;
  request.length=2; assert(host_rf_handle(&request)==SPI_STATUS_INVALID_ARG); request.length=1;
  s_is_bad_uid=true; assert(host_rf_handle(&request)==SPI_STATUS_ERROR && request.out_length==0); s_is_bad_uid=false;
  s_poll_error=ESP_ERR_NOT_FOUND;
  assert(host_rf_handle(&request)==SPI_STATUS_OK && request.out_length==1 && output[0]==0);
  s_poll_error=ESP_FAIL; assert(host_rf_handle(&request)==SPI_STATUS_ERROR && s_locks==0);
  s_poll_error=ESP_OK;
  s_init_error=ESP_FAIL; assert(host_rf_handle(&request)==SPI_STATUS_ERROR && s_locks==0); s_init_error=ESP_OK;
  s_pm_error=ESP_FAIL; assert(host_rf_handle(&request)==SPI_STATUS_ERROR && s_locks==0); s_pm_error=ESP_OK;
  s_off_error=ESP_FAIL;
  assert(host_rf_handle(&request)==SPI_STATUS_ERROR && request.out_length==0 && s_locks==1);
  assert(rf_frontend_claim(RF_FRONTEND_LF_125KHZ)==ESP_ERR_INVALID_STATE);
  request.command=SPI_ID_NFC_STATUS; request.length=0;
  assert(host_rf_handle(&request)==SPI_STATUS_OK && output[0]==1 && output[1]==RF_FRONTEND_NFC_13MHZ);
  request.command=SPI_ID_RFID_RECOVER; assert(host_rf_handle(&request)==SPI_STATUS_BUSY);
  host_rf_stop(); assert(s_locks==1);
  s_off_error=ESP_OK;
  host_rf_stop(); assert(s_locks==0 && rf_frontend_get_mode()==RF_FRONTEND_IDLE);
  request.command=SPI_ID_NFC_RECOVER; assert(host_rf_handle(&request)==SPI_STATUS_OK);
  request.command=SPI_ID_RFID_READ; request.length=2; payload[0]=0xF4; payload[1]=1;
  assert(host_rf_handle(&request)==SPI_STATUS_OK && request.out_length==9);
  assert(memcmp(output+1,"ABCDE",5)==0 && output[6]==1 && output[7]==0x34 && output[8]==0x12);
  payload[1]=2; assert(host_rf_handle(&request)==SPI_STATUS_INVALID_ARG); payload[1]=1;
  s_pm_release_error=ESP_FAIL;
  assert(host_rf_handle(&request)==SPI_STATUS_ERROR && s_locks==1);
  assert(rf_frontend_get_mode()==RF_FRONTEND_IDLE);
  assert(host_rf_handle(&request)==SPI_STATUS_BUSY);
  s_pm_release_error=ESP_OK; host_rf_stop(); assert(s_locks==0);
  request.command=SPI_CMD(SPI_CAT_RFID,0xFF); assert(host_rf_handle(&request)==SPI_STATUS_UNSUPPORTED);
  assert(rf_frontend_reserve(RF_FRONTEND_LF_125KHZ)==ESP_OK);
  assert(rf_frontend_unreserve(RF_FRONTEND_LF_125KHZ)==ESP_OK);
  assert(s_stops>0 && s_reads>0);
  puts("PASS: NFC A/B/F/V and LF wire results, strict bounds, contention, cleanup and retained failed-OFF/no-sleep recovery");
}
