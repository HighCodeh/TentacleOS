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
#include <stdio.h>
#include <string.h>

#include "tos_api.h"
#include "host_link_local_rf.h"
#include "spi_protocol.h"

extern const tos_nfc_api_t tos_nfc_api_impl;
extern const tos_rfid_api_t tos_rfid_api_impl;
static bool s_has_cap, s_stopped, s_can_lock=true, s_stop_on_lock;
static int s_locked, s_calls, s_found_tech=0;
static int64_t s_now;
static esp_err_t s_error;
static bool s_bad_result;
static int should_stop(void) { return s_stopped; }
const tos_api_t *tos_api_get(void) { static const tos_api_t api={.should_stop=should_stop}; return &api; }
bool tos_app_cap_check(uint32_t cap) { assert(cap==TOS_CAP_RADIO_RX || cap==TOS_CAP_RADIO_TX); return s_has_cap; }
bool host_local_rf_lock(uint32_t wait) {
  assert(wait==3000); if(!s_can_lock) return false;
  assert(s_locked==0); s_locked=1; if(s_stop_on_lock) s_stopped=true; return true;
}
void host_local_rf_unlock(void) { assert(s_locked==1); s_locked=0; }
int64_t esp_timer_get_time(void) { return s_now; }
esp_err_t host_local_rf_execute(host_rf_request_t *request) {
  assert(s_locked==1); ++s_calls; s_now+=1000;
  assert(request->capacity==HOST_RF_RESPONSE_MAX);
  if(s_error) return s_error;
  memset(request->out,0,request->capacity);
  if(request->command==SPI_ID_RFID_READ) {
    assert(request->length==2);
    uint32_t timeout=request->payload[0] | ((uint32_t)request->payload[1]<<8);
    assert(timeout>=40 && timeout<=500);
    request->out_length=9; request->out[0]=1;
    for(int i=0;i<5;i++) request->out[i+1]=(uint8_t)(i+1);
  } else {
    assert(request->command==SPI_ID_NFC_READ && request->length==1);
    uint8_t tech=request->payload[0];
    if(tech!=s_found_tech) { request->out_length=1; return ESP_OK; }
    request->out_length=17; request->out[0]=1; request->out[3]=4;
    memcpy(request->out+4,"ABCD",4); request->out[14]=0x44; request->out[16]=0x20;
  }
  if(s_bad_result) request->out_length=2;
  return ESP_OK;
}
esp_err_t host_local_rf_call(host_local_rf_cb_t callback,void *context) {assert(s_locked);return callback(context);}
esp_err_t host_rf_nfc_block(host_rf_nfc_block_t *request) {
 assert(s_locked && request->block==4 && request->key[0]==0xFF);++s_calls;
 if(s_error)return s_error;
 if (request->is_write) {
   assert(request->data[0]==0xCA);
 } else {
   memset(request->data,0xCA,sizeof(request->data));
 }
 return ESP_OK;
}
int main(void) {
  tos_rfid_tag_t rfid; tos_nfc_tag_t nfc;
  assert(tos_rfid_api_impl.read(&rfid,500)==ESP_ERR_NOT_SUPPORTED && s_calls==0);
  s_has_cap=true; s_stopped=true;
  assert(tos_rfid_api_impl.read(&rfid,500)==ESP_ERR_INVALID_STATE && s_calls==0);
  s_stopped=false; s_can_lock=false;
  assert(tos_rfid_api_impl.read(&rfid,500)==ESP_ERR_TIMEOUT && s_calls==0);
  s_can_lock=true; s_stop_on_lock=true;
  assert(tos_rfid_api_impl.read(&rfid,500)==ESP_ERR_INVALID_STATE && s_calls==0 && s_locked==0);
  s_stop_on_lock=false; s_stopped=false;
  assert(tos_rfid_api_impl.read(&rfid,39)==ESP_ERR_INVALID_ARG);
  assert(tos_rfid_api_impl.read(&rfid,501)==ESP_ERR_INVALID_ARG);
  assert(tos_rfid_api_impl.read(NULL,500)==ESP_ERR_INVALID_ARG);
  assert(tos_rfid_api_impl.read(&rfid,500)==ESP_OK);
  assert(strcmp(rfid.id,"0102030405")==0 && strcmp(rfid.protocol,"EM4100")==0);
  assert(rfid.raw_value==UINT64_C(0x0102030405) && rfid.bit_count==40);
  assert(rfid.card_number==0x02030405 && rfid.facility_code==0 && s_locked==0);
  uint8_t expected[]={TOS_NFC_ISO14443A,TOS_NFC_ISO14443B,TOS_NFC_FELICA,TOS_NFC_ISO15693};
  for(int tech=0;tech<4;tech++) {
    s_found_tech=tech; int calls=s_calls;
    assert(tos_nfc_api_impl.poll(&nfc,500)==ESP_OK && s_calls==calls+tech+1);
    assert(nfc.uid_len==4 && memcmp(nfc.uid,"ABCD",4)==0 && nfc.protocol==expected[tech]);
    assert(nfc.atqa[0]==0x44 && nfc.sak==0x20 && s_locked==0);
  }
  uint8_t key[6]={0xFF},data[16]={0};
  assert(tos_nfc_api_impl.read_block(4,key,data)==ESP_OK && data[0]==0xCA);
  assert(tos_nfc_api_impl.write_block(4,key,data)==ESP_OK);
  s_error=ESP_FAIL;assert(tos_nfc_api_impl.read_block(4,key,data)==ESP_FAIL && data[0]==0);s_error=0;
  s_has_cap=false;assert(tos_nfc_api_impl.write_block(4,key,data)==ESP_ERR_NOT_SUPPORTED);s_has_cap=true;
  assert(tos_nfc_api_impl.read_block(4,NULL,data)==ESP_ERR_INVALID_ARG);
  assert(tos_nfc_api_impl.emulate_start!=NULL && tos_nfc_api_impl.session_stop!=NULL);
  s_found_tech=-1; int calls=s_calls;
  assert(tos_nfc_api_impl.poll(&nfc,500)==ESP_ERR_NOT_FOUND && s_calls==calls+4);
  calls=s_calls;
  assert(tos_nfc_api_impl.poll(&nfc,1)==ESP_ERR_NOT_FOUND && s_calls==calls+1);
  s_found_tech=0; s_bad_result=true;
  assert(tos_nfc_api_impl.poll(&nfc,500)==ESP_ERR_INVALID_SIZE);
  assert(tos_rfid_api_impl.read(&rfid,500)==ESP_ERR_INVALID_SIZE);
  s_bad_result=false; s_error=ESP_FAIL;
  assert(tos_rfid_api_impl.read(&rfid,500)==ESP_FAIL && s_locked==0);
  puts("PASS: native RF ABI capabilities, cancellation, time bounds, A/B/F/V mapping and LF records");
}

esp_err_t tos_api_nfc_session_start(const uint8_t *uid, uint8_t len, uint32_t *out) { (void)uid; (void)len; (void)out; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t tos_api_nfc_session_stop(uint32_t id) { (void)id; return ESP_ERR_NOT_SUPPORTED; }
