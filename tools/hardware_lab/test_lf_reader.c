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
#include "lf_rfid.h"
#include "lf_rfid_decoder.h"
#define DRAM_ATTR
#define IRAM_ATTR
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define GPIO_RFID_LF_DATA_PIN 16
#define RF_FRONTEND_LF_125KHZ 1
#define MCPWM_CAP_EDGE_NEG 0
#define MCPWM_CAP_EDGE_POS 1
#define MCPWM_CAPTURE_CLK_SRC_DEFAULT 0
#define pdMS_TO_TICKS(x) (x)
typedef void *mcpwm_cap_channel_handle_t;
typedef void *mcpwm_cap_timer_handle_t;
typedef struct {uint32_t cap_value;int cap_edge;} mcpwm_capture_event_data_t;
typedef struct {int group_id,clk_src;uint32_t resolution_hz;} mcpwm_capture_timer_config_t;
typedef struct {int gpio_num,prescale;struct{bool pos_edge,neg_edge;}flags;}mcpwm_capture_channel_config_t;
typedef struct {bool(*on_cap)(void*,const mcpwm_capture_event_data_t*,void*);}mcpwm_capture_event_callbacks_t;
static mcpwm_capture_event_callbacks_t callback;
static bool field=true,allowed=true,cap_on;
static int failure;
static unsigned stall_ms;
static uint64_t clock_us;
static uint32_t cap_tick;
static uint32_t durations[1600];
static uint8_t levels[1600];
static unsigned waves,wave;
static uint64_t next_edge;
static int op(int id){return id==failure?ESP_FAIL:ESP_OK;}
bool lf_rfid_field_is_on(void){return field;}
uint32_t lf_rfid_field_age_ms(void){return 450;}
static bool rf_frontend_can_access(int m){assert(m==1);return allowed;}
static int64_t esp_timer_get_time(void){return clock_us;}
static int mcpwm_new_capture_timer(const mcpwm_capture_timer_config_t*c,void**h){assert(c->resolution_hz==1000000);if(op(1))return ESP_FAIL;*h=(void*)1;return 0;}
static int mcpwm_capture_timer_get_resolution(void*h,uint32_t*out){assert(h);*out=1000000;return op(2);}
static int mcpwm_new_capture_channel(void*h,const mcpwm_capture_channel_config_t*c,void**out){assert(h&&c->gpio_num==16&&c->flags.pos_edge&&c->flags.neg_edge);if(op(3))return ESP_FAIL;*out=(void*)2;return 0;}
static int mcpwm_capture_channel_register_event_callbacks(void*h,const mcpwm_capture_event_callbacks_t*c,void*ctx){assert(h&&!ctx);callback=*c;return op(4);}
static int mcpwm_capture_timer_enable(void*h){assert(h);return op(5);}
static int mcpwm_capture_timer_start(void*h){assert(h);return op(6);}
static int mcpwm_capture_channel_enable(void*h){assert(h);if(op(7))return ESP_FAIL;cap_on=true;wave=0;cap_tick=UINT32_MAX-1024;next_edge=clock_us;return 0;}
static int mcpwm_capture_channel_disable(void*h){assert(h);if(op(8))return ESP_FAIL;cap_on=false;return 0;}
static int mcpwm_del_capture_channel(void*h){assert(h&&!cap_on);return op(9);}
static int mcpwm_capture_timer_stop(void*h){assert(h);return op(10);}
static int mcpwm_capture_timer_disable(void*h){assert(h);return op(11);}
static int mcpwm_del_capture_timer(void*h){assert(h);return op(12);}
static void vTaskDelay(unsigned ms){
 if(stall_ms){ms=stall_ms;stall_ms=0;}
 uint64_t end=clock_us+ms*1000;
 while(cap_on&&wave<=waves&&next_edge<=end){
  mcpwm_capture_event_data_t event={.cap_value=cap_tick,.cap_edge=wave==0?MCPWM_CAP_EDGE_POS:levels[wave-1]?MCPWM_CAP_EDGE_NEG:MCPWM_CAP_EDGE_POS};
  callback.on_cap((void*)2,&event,NULL);
  if(wave==waves){wave++;break;}
  next_edge+=durations[wave];cap_tick+=durations[wave];wave++;
 }
 clock_us=end;
}
/* READER */
static void signal(uint64_t frame,unsigned half,unsigned invert,unsigned repeat){
 waves=wave=0;clock_us=0;cap_on=false;
 for(unsigned r=0;r<repeat;r++)for(int bit=63;bit>=0;bit--){
  unsigned first=((frame>>bit)&1)^invert;
  for(unsigned h=0;h<2;h++){
   unsigned level=first^h;
   if(waves&&levels[waves-1]==level)durations[waves-1]+=half;
   else{assert(waves<1600);levels[waves]=level;durations[waves++]=half;}
  }
 }
}
int main(void){
 // Independently assembled EM4100 parity frame for ID 0102030405.
 const uint64_t frame=UINT64_C(0xFF8060280C048142);
 lf_rfid_em4100_t tag;
 for(unsigned invert=0;invert<2;invert++)for(unsigned half=248;half<=264;half++){
  signal(frame,half,invert,8);stall_ms=20;
  assert(lf_rfid_read_em4100(500,&tag)==0);
  assert(tag.id==UINT64_C(0x0102030405)&&!cap_on&&s_timer==NULL&&s_channel==NULL);
 }
 signal(frame,256,0,2);assert(lf_rfid_read_em4100(500,&tag)==ESP_ERR_NOT_FOUND&&tag.id==0);
 signal(frame^UINT64_C(0x1000),256,0,8);assert(lf_rfid_read_em4100(500,&tag)==ESP_ERR_NOT_FOUND);
 signal(frame,256,0,8);stall_ms=400;assert(lf_rfid_read_em4100(500,&tag)==ESP_ERR_INVALID_SIZE&&tag.id==0);
 lf_rfid_read_stats_t stats;lf_rfid_read_stats(&stats);assert(stats.dropped&&!stats.running);
 signal(frame,256,0,0);assert(lf_rfid_read_em4100(40,&tag)==ESP_ERR_NOT_FOUND);
 field=false;assert(lf_rfid_read_em4100(500,&tag)==ESP_ERR_INVALID_STATE);field=true;
 allowed=false;assert(lf_rfid_read_em4100(500,&tag)==ESP_ERR_INVALID_STATE);allowed=true;
 assert(lf_rfid_read_em4100(1,&tag)==ESP_ERR_INVALID_ARG);
 for(failure=1;failure<=12;failure++){
  signal(frame,256,0,8);assert(lf_rfid_read_em4100(500,&tag)!=0);
  int saved=failure;failure=0;assert(lf_rfid_reader_quiesce()==0);failure=saved;
  assert(!cap_on&&!s_timer&&!s_channel);
 }
 failure=0;signal(frame,256,0,8);assert(lf_rfid_read_em4100(500,&tag)==0);
 puts("PASS: real reader, hardware timestamps across wrap/stalls, parity, repeats, overflow, no signal, access and 12 failures/cleanup/retry");
}
