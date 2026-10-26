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
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "host_link.h"
#include "host_link_dispatch.h"
#include "host_link_files.h"
#include "host_link_rf.h"
#include "spi_protocol.h"

#define HOST_LINK_BODY_HDR 3
#define ESP_LOGW(tag, ...) ((void)(tag))
static const char *TAG="TEST_RF_ROUTING";
static portMUX_TYPE s_state_lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation=1, s_ready_generation=1, s_dispatch_generation=1;
static bool s_is_authenticated, s_is_valid_mac, s_finish_allowed;
static unsigned s_ready_responses;
static void *s_lock=(void *)1;
#define portMAX_DELAY 0
#define xSemaphoreTakeRecursive(lock, wait) ((void)(lock), (void)(wait))
#define xSemaphoreGiveRecursive(lock) ((void)(lock))
static bool host_link_sec_finish(const uint8_t *span,size_t length,const uint8_t *mac,uint32_t counter) {
  assert(span && length && mac && counter==1);return s_finish_allowed;
}
static void emit_frame(uint32_t generation,uint8_t type,uint8_t category,uint8_t op,const uint8_t *payload,uint16_t length) {
  assert(generation==1 && type==HOST_TYPE_HELLO_READY && category==0 && op==0 && !payload && !length);++s_ready_responses;
}
static unsigned s_calls, s_responses, s_relays;
static bool host_link_sec_is_authenticated(void) { return s_is_authenticated; }
static bool host_link_sec_verify_inbound(const uint8_t *data,size_t length,const uint8_t *mac,uint32_t counter) {
  assert(data!=NULL && length>0 && mac!=NULL && counter==1);
  return s_is_valid_mac;
}
static void handle_hello(const uint8_t *payload,uint16_t length) { (void)payload; (void)length; }
bool host_link_dispatch_lookup(uint16_t command,host_link_handler_t *out) { (void)command; (void)out; return false; }
static void send_resp(uint8_t category,uint8_t operation,uint8_t status,const uint8_t *data,uint16_t length) {
  (void)operation;
  assert(s_is_authenticated && s_is_valid_mac && s_dispatch_generation==s_ready_generation);
  assert(category==SPI_CAT_NFC || category==SPI_CAT_RFID);
  assert(status==SPI_STATUS_OK && length==1 && data[0]==0); ++s_responses;
}
static void dispatch_cmd(uint8_t category,uint8_t operation,const uint8_t *data,uint8_t length) {
  (void)category; (void)operation; (void)data; (void)length; ++s_relays;
}
uint8_t host_rf_handle(host_rf_request_t *request) {
  assert(request->command==SPI_ID_NFC_READ || request->command==SPI_ID_RFID_READ);
  assert(request->capacity==HOST_RF_RESPONSE_MAX);
  request->out[0]=0; request->out_length=1; ++s_calls; return SPI_STATUS_OK;
}
/* PROCESS_FRAME */
int main(void) {
  uint8_t frame[HOST_LINK_HDR_SIZE+HOST_LINK_BODY_HDR+HOST_LINK_MAC_SIZE]={0};
  frame[2]=HOST_LINK_VER; frame[4]=1; frame[8]=HOST_LINK_BODY_HDR;
  frame[10]=HOST_TYPE_CMD; frame[11]=SPI_CAT_NFC; frame[12]=1;
  process_frame(frame,sizeof(frame)); assert(s_calls==0);
  s_is_authenticated=true; s_is_valid_mac=true;
  process_frame(frame,sizeof(frame)); assert(s_calls==0);
  frame[3]=HOST_LINK_FLAG_AUTH; s_is_authenticated=false;
  process_frame(frame,sizeof(frame)); assert(s_calls==0);
  s_is_authenticated=true; s_is_valid_mac=false;
  process_frame(frame,sizeof(frame)); assert(s_calls==0);
  s_is_valid_mac=true; s_dispatch_generation=2;
  process_frame(frame,sizeof(frame)); assert(s_calls==0);
  s_dispatch_generation=1; s_ready_generation=0;
  process_frame(frame,sizeof(frame)); assert(s_calls==0);
  s_ready_generation=1;
  process_frame(frame,sizeof(frame)); assert(s_calls==1 && s_responses==1 && s_relays==0);
  frame[11]=SPI_CAT_RFID;
  process_frame(frame,sizeof(frame)); assert(s_calls==2 && s_responses==2 && s_relays==0);
  frame[10]=HOST_TYPE_HELLO_FINISH;frame[11]=0;frame[12]=0;
  s_is_authenticated=false;s_finish_allowed=false;process_frame(frame,sizeof(frame));assert(s_ready_responses==0);
  s_finish_allowed=true;process_frame(frame,sizeof(frame));assert(s_ready_responses==1 && s_calls==2);
  frame[12]=1;process_frame(frame,sizeof(frame));assert(s_ready_responses==1);
  puts("PASS: production host-link RF routing rejects unauthenticated, failed-MAC/replay and stale-generation commands");
}
