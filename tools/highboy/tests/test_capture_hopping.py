#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Run capture scheduling, lab filtering and one-shot TX with host radio mocks."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/"firmware_c5/components/Service/wifi/wifi_service.c").read_text()
header=(ROOT/"firmware_c5/components/Service/wifi/include/wifi_service.h").read_text()
start=header.index("typedef enum {\n  WIFI_SERVICE_CAPTURE_AP")
types=header[start:header.index("} wifi_service_capture_activity_t;",start)+len("} wifi_service_capture_activity_t;")]
types += "\n" + header[header.index("#define WIFI_SERVICE_CAPTURE_MAC_LEN"):header.index("/** Configure only while")]
start=source.index("typedef struct {\n  bool is_capture;")
state=source[start:source.index("static TaskHandle_t s_hopper_task_handle",start)]
start=source.index("esp_err_t wifi_service_set_capture_hopping(")
code=source[start:source.index("void wifi_service_start_channel_hopping(",start)]
defines="\n".join(line for line in source.splitlines() if re.match(r"#define (CAPTURE_HOP_|HOPPER_DELAY_MS|CAPTURE_US_PER_MS)",line))
fakes=r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -4
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))

#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static unsigned allocations, live_allocations, fail_at;
static void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
 assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
 if (++allocations == fail_at) return NULL;
 void *p = calloc(count, size); assert(p); live_allocations++; return p;
}
static void tracked_free(void *p) { if(p) { assert(live_allocations);live_allocations--; } free(p); }
#define free tracked_free

static int64_t now;
static int64_t esp_timer_get_time(void) {return now;}
"""
tests=r"""
static const uint8_t PLAN[] = {1,6,11,36};
static const uint8_t AP1[] = {2,0,0,0,0,1};
static const uint8_t AP2[] = {2,0,0,0,0,2};
static void test_selection(void) {
 now=0;wifi_service_set_capture_hopping(true);
 for(size_t i=0;i<sizeof(PLAN);i++) {
  assert(select_hop(PLAN,sizeof(PLAN),0)==i);begin_hop(PLAN[i]);
  if(PLAN[i]==6) {
   for(int j=0;j<100;j++)wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_AP,AP1);
   assert(s_hop_channels[i].score==1);
   wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_AP,AP2);assert(s_hop_channels[i].score==2);
   for(int j=0;j<100;j++)wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_ASSOC,AP1);
   assert(s_hop_channels[i].score==6);
   for(int j=0;j<100;j++)wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_EAPOL,AP1);
   assert(s_hop_channels[i].score==14);
   wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_ACTIVITY_COUNT,AP1);
   wifi_service_observe_capture(6,(wifi_service_capture_activity_t)-1,AP1);
   assert(s_hop_channels[i].score==14);
  }
  now=s_hop_state.deadline_us;assert(finish_hop());
 }
 assert(select_hop(PLAN,sizeof(PLAN),0)==1);
 begin_hop(6);wifi_service_observe_capture(11,WIFI_SERVICE_CAPTURE_EAPOL,AP1);
 assert(s_hop_channels[2].score==0);
 // Repeated selection cannot indefinitely starve low-scoring channels.
 int64_t last[sizeof(PLAN)]={0};unsigned visits[sizeof(PLAN)]={0};
 for(int step=0;step<500;step++) {
  size_t index=select_hop(PLAN,sizeof(PLAN),0);begin_hop(PLAN[index]);
  if(visits[index])assert(now-last[index]<=CAPTURE_HOP_REVISIT_US+
                          (int64_t)sizeof(PLAN)*CAPTURE_HOP_MAX_MS*CAPTURE_US_PER_MS);
  last[index]=now;visits[index]++;
  if(PLAN[index]==6)wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_EAPOL,AP1);
  now=s_hop_state.deadline_us;assert(finish_hop());
 }
 for(size_t i=0;i<sizeof(PLAN);i++)assert(visits[i]>1);
 assert(visits[1]>visits[0]);
 // Fixed-point history decays independently of the receive clock frequency.
 s_hop_channels[1].score=64;s_hop_channels[1].decay_us=now;
 now+=CAPTURE_HOP_DECAY_US;decay_score(&s_hop_channels[1],now);assert(s_hop_channels[1].score==32);
 now+=CAPTURE_HOP_DECAY_US*CAPTURE_HOP_SCORE_BITS;decay_score(&s_hop_channels[1],now);
 assert(s_hop_channels[1].score==0);
 const uint8_t changed[]={149,153};assert(select_hop(changed,2,0)==0);
 assert(s_hop_channel_count==2 && s_hop_channels[0].score==0 && s_hop_aps[0].channel==0);
 assert(select_hop(changed,2,0)==1);
 wifi_service_set_capture_hopping(false);assert(s_hop_channel_count==0);
 assert(select_hop(PLAN,sizeof(PLAN),3)==3);
 wifi_service_set_capture_hopping(true);assert(select_hop(PLAN,sizeof(PLAN),0)==0);
 begin_hop(1);
 for(unsigned j=0;j<200;j++) {
  uint8_t ap[]={2,0,0,0,1,(uint8_t)j};
  wifi_service_observe_capture(1,WIFI_SERVICE_CAPTURE_AP,ap);
 }
 assert(s_hop_channels[0].score==CAPTURE_HOP_SCORE_MAX);
 uint8_t multicast[]={1,0,0,0,0,1},zero[6]={0};
 s_hop_channels[0].score=0;
 wifi_service_observe_capture(1,WIFI_SERVICE_CAPTURE_AP,multicast);
 wifi_service_observe_capture(1,WIFI_SERVICE_CAPTURE_AP,zero);
 assert(s_hop_channels[0].score==0);
}
static unsigned tx_calls;
static int tx_result;
static esp_err_t transmit(const uint8_t *bssid, uint8_t channel) {
 assert(channel==1);assert(memcmp(bssid,AP1,6)==0 || memcmp(bssid,AP2,6)==0);
 tx_calls++;now+=1000;return tx_result;
}
static const uint8_t SECURED[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
static void test_security_ie(void) {
 assert(has_secured_ie(SECURED,sizeof(SECURED)));
 for(size_t i=0;i<sizeof(SECURED);i++)assert(!has_secured_ie(SECURED,i));
 const uint8_t open[]={0,3,'a','p','1'},empty_rsn[]={48,0},bad_version[]={48,2,2,0};
 assert(!has_secured_ie(open,sizeof(open)));
 assert(!has_secured_ie(empty_rsn,sizeof(empty_rsn)));
 assert(!has_secured_ie(bad_version,sizeof(bad_version)));
 assert(!has_secured_ie(NULL,0));
 uint8_t bad[sizeof(SECURED)*2+1];memcpy(bad,SECURED,sizeof(SECURED));
 bad[sizeof(SECURED)]=0;assert(!has_secured_ie(bad,sizeof(SECURED)+1));
 memcpy(bad+sizeof(SECURED),SECURED,sizeof(SECURED));
 assert(!has_secured_ie(bad,sizeof(SECURED)*2));
 memcpy(bad,SECURED,sizeof(SECURED));bad[8]=0;assert(!has_secured_ie(bad,sizeof(SECURED)));
 bad[8]=255;bad[9]=255;assert(!has_secured_ie(bad,sizeof(SECURED)));
 memcpy(bad,SECURED,sizeof(SECURED));bad[14]=255;assert(!has_secured_ie(bad,sizeof(SECURED)));
 uint8_t wpa[26]={221,24,0,80,242,1};memcpy(wpa+6,SECURED+2,20);
 assert(has_secured_ie(wpa,sizeof(wpa)));wpa[6]=2;assert(!has_secured_ie(wpa,sizeof(wpa)));
 const uint8_t wpa_short[]={221,4,0,80,242,1};assert(!has_secured_ie(wpa_short,sizeof(wpa_short)));
 uint8_t pmkid[44]={0};memcpy(pmkid,SECURED,sizeof(SECURED));pmkid[1]=42;
 pmkid[22]=1;pmkid[24]=1; // one PMKID, then a management suite
 assert(has_secured_ie(pmkid,sizeof(pmkid)));
 pmkid[22]=2;assert(!has_secured_ie(pmkid,sizeof(pmkid)));
}
static void test_lab_holds(void) {
 now=0;wifi_service_set_capture_hopping(true);select_hop(PLAN,sizeof(PLAN),0);begin_hop(1);
 wifi_service_observe_capture_ap(1,AP1,SECURED,sizeof(SECURED));assert(!s_hop_state.has_ap_hold);
 wifi_service_set_capture_action(transmit);
 wifi_service_observe_capture_ap(6,AP1,SECURED,sizeof(SECURED));assert(!s_hop_state.has_ap_hold);
 const uint8_t invalid[]={1,0,0,0,0,1},zero[6]={0};
 wifi_service_observe_capture_ap(1,invalid,SECURED,sizeof(SECURED));
 wifi_service_observe_capture_ap(1,zero,SECURED,sizeof(SECURED));assert(!s_hop_state.has_ap_hold);
 for(unsigned attempt=1;attempt<=CAPTURE_HOP_LAB_ATTEMPTS;attempt++) {
  wifi_service_observe_capture_ap(1,AP1,SECURED,sizeof(SECURED));
  assert(s_hop_state.has_ap_hold && s_hop_labs[0].attempts==attempt);
  assert(tx_calls==attempt-1); // RX only schedules, never transmits.
  tx_result=attempt==2?ESP_FAIL:ESP_OK;
  process_capture_action();assert(tx_calls==attempt);
  int64_t deadline=s_hop_state.deadline_us;
  assert(deadline==now+CAPTURE_HOP_LAB_HOLD_US);
  wifi_service_capture_status_t status;wifi_service_get_capture_status(&status);
  assert(status.hold_seconds==15 && status.ap_attempt==attempt);
  assert(status.attempts==1 && status.failures==(attempt==2));
  now=deadline-1;
  wifi_service_observe_capture_ap(1,AP1,SECURED,sizeof(SECURED));
  wifi_service_observe_capture_ap(1,AP2,SECURED,sizeof(SECURED));
  wifi_service_observe_capture(1,WIFI_SERVICE_CAPTURE_EAPOL,AP1);
  process_capture_action();assert(tx_calls==attempt);
  assert(s_hop_state.deadline_us==deadline && !finish_hop());
  now=deadline;assert(finish_hop());
  wifi_service_set_capture_hopping(false);wifi_service_set_capture_hopping(true);
  wifi_service_set_capture_action(transmit);select_hop(PLAN,sizeof(PLAN),0);begin_hop(1);
 }
 wifi_service_observe_capture_ap(1,AP1,SECURED,sizeof(SECURED));assert(!s_hop_state.has_ap_hold);
 wifi_service_observe_capture_ap(1,AP2,SECURED,sizeof(SECURED));assert(s_hop_state.has_ap_hold);
 wifi_service_set_capture_hopping(false);process_capture_action();assert(tx_calls==3);
 // Stop cancels pending TX, but cannot give the BSSID its attempt back.
 assert(s_hop_labs[1].attempts==1);
 wifi_service_set_capture_hopping(true);wifi_service_set_capture_action(transmit);begin_hop(1);
 for(size_t i=0;i<CAPTURE_HOP_LAB_CAPACITY;i++)s_hop_labs[i].attempts=3;
 const uint8_t other[]={6,0,0,0,0,1};
 wifi_service_observe_capture_ap(1,other,SECURED,sizeof(SECURED));assert(!s_hop_state.has_ap_hold);
}
static void test_captured_bssids(void) {
 memset(s_hop_labs,0,sizeof(s_hop_labs));tx_calls=0;now=0;
 wifi_service_set_capture_hopping(true);wifi_service_set_capture_action(transmit);begin_hop(1);
 wifi_service_mark_captured(AP1);wifi_service_mark_captured(AP1);
 assert(s_hop_labs[0].is_captured && s_hop_labs[0].attempts==0);
 wifi_service_observe_capture_ap(1,AP1,SECURED,sizeof(SECURED));assert(!s_hop_state.has_action_pending);
 wifi_service_observe_capture_ap(1,AP2,SECURED,sizeof(SECURED));assert(s_hop_state.has_action_pending);
 wifi_service_mark_captured(AP1);assert(s_hop_state.has_action_pending);
 wifi_service_mark_captured(AP2);assert(!s_hop_state.has_action_pending);
 process_capture_action();assert(tx_calls==0);
 assert(s_hop_labs[1].is_captured && s_hop_labs[1].attempts==1);
 wifi_service_set_capture_hopping(false);wifi_service_set_capture_hopping(true);
 wifi_service_set_capture_action(transmit);begin_hop(1);
 wifi_service_observe_capture_ap(1,AP1,SECURED,sizeof(SECURED));
 wifi_service_observe_capture_ap(1,AP2,SECURED,sizeof(SECURED));assert(!s_hop_state.has_action_pending);
 for(unsigned i=2;i<CAPTURE_HOP_LAB_CAPACITY;i++) {
  uint8_t ap[]={2,0,0,1,0,(uint8_t)i};wifi_service_mark_captured(ap);
 }
 const uint8_t unknown[]={2,0,0,2,0,1};
 wifi_service_mark_captured(unknown);
 wifi_service_observe_capture_ap(1,unknown,SECURED,sizeof(SECURED));
 assert(!s_hop_state.has_action_pending);
 for(unsigned i=0;i<CAPTURE_HOP_LAB_CAPACITY;i++)assert(s_hop_labs[i].is_captured);
}
int main(void) {
 assert(wifi_service_set_capture_hopping(false)==ESP_OK && allocations==0);
 fail_at=1;assert(wifi_service_set_capture_hopping(true)==ESP_ERR_NO_MEM);
 assert(!s_hop_channels && !s_hop_aps && !s_hop_state.is_capture && !live_allocations);
 allocations=0;fail_at=2;
 assert(wifi_service_set_capture_hopping(true)==ESP_ERR_NO_MEM);
 assert(!s_hop_channels && !s_hop_aps && !s_hop_state.is_capture && !live_allocations);
 allocations=0;fail_at=0;assert(wifi_service_set_capture_hopping(true)==ESP_OK);
 assert(live_allocations==2 && allocations==2);
 s_hop_channels[CAPTURE_HOP_CHANNEL_CAPACITY-1].score=1;
 s_hop_aps[CAPTURE_HOP_AP_CAPACITY-1].channel=1;
 assert(wifi_service_set_capture_hopping(false)==ESP_OK);
 assert(wifi_service_set_capture_hopping(true)==ESP_OK && allocations==2);
 assert(!s_hop_channels[CAPTURE_HOP_CHANNEL_CAPACITY-1].score);
 assert(!s_hop_aps[CAPTURE_HOP_AP_CAPACITY-1].channel);

 test_radio_adapter();test_security_ie();test_selection();test_lab_holds();test_captured_bssids();now=0;
 wifi_service_set_capture_hopping(false);begin_hop(6);
 wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_EAPOL,NULL);
 now=249000;assert(!finish_hop());now=250000;assert(finish_hop());
 wifi_service_set_capture_hopping(true);now=0;begin_hop(6);
 now=119000;assert(!finish_hop());now=120000;assert(finish_hop());
 // A queued event after committing a hop must not revive the old channel.
 wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_EAPOL,NULL);assert(s_hop_state.channel==0);
 now=0;begin_hop(6);wifi_service_observe_capture(11,WIFI_SERVICE_CAPTURE_EAPOL,NULL);
 assert(s_hop_state.deadline_us==120000);
 wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_AP,NULL);assert(s_hop_state.deadline_us==200000);
 now=190000;wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_ASSOC,NULL);
 assert(s_hop_state.deadline_us==940000);
 now=900000;wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_EAPOL,NULL);
 assert(s_hop_state.deadline_us==1900000);
 wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_AP,NULL);assert(s_hop_state.deadline_us==1900000);
 now=1800000;wifi_service_observe_capture(6,WIFI_SERVICE_CAPTURE_EAPOL,NULL);
 assert(s_hop_state.deadline_us==2000000);
 now=1999999;assert(!finish_hop());now=2000000;assert(finish_hop());
 wifi_service_set_capture_hopping(false);assert(!s_hop_state.is_capture && !s_hop_state.channel);
 // 64-bit clock remains valid after a 32-bit millisecond wrap.
 now=5000000000000LL;wifi_service_set_capture_hopping(true);begin_hop(36);
 now+=120000;assert(finish_hop());
 return 0;
}
"""

radio_source=(ROOT/"firmware_c5/components/Applications/wifi/wifi_deauther.c").read_text()
radio_defines="\n".join(line for line in radio_source.splitlines()
                         if re.match(r"#define (DEAUTH_FRAME_LEN|DEAUTH_SOURCE_OFFSET|DEAUTH_BSSID_OFFSET|BSSID_LEN)",line))
a=radio_source.index("static const uint8_t DEAUTH_FRAME_INACTIVITY[]")
b=radio_source.index(";",a)+1
radio_template=radio_source[a:b]
a=radio_source.index("esp_err_t wifi_deauther_send_capture_frame(")
b=radio_source.index("void wifi_deauther_send_raw_frame(",a)
radio_code=radio_source[a:b]
radio_fakes=r"""
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_STATE -3
#define WIFI_IF_AP 1
typedef int wifi_second_chan_t;
static bool s_is_running;
static int get_result, send_result;
static uint8_t actual_channel;
static unsigned sent_frames;
static uint8_t sent_frame[26];
static int esp_wifi_get_channel(uint8_t *channel, wifi_second_chan_t *second) {
 *channel=actual_channel;*second=0;return get_result;
}
static int esp_wifi_80211_tx(int interface,const void *frame,int size,bool sequence) {
 assert(interface==WIFI_IF_AP && size==26 && !sequence);
 sent_frames++;memcpy(sent_frame,frame,sizeof(sent_frame));return send_result;
}
"""
radio_tests=r"""
static void test_radio_adapter(void) {
 const uint8_t ap[]={2,1,2,3,4,5};
 actual_channel=6;
 assert(wifi_deauther_send_capture_frame(NULL,6)==ESP_ERR_INVALID_ARG);
 assert(wifi_deauther_send_capture_frame(ap,0)==ESP_ERR_INVALID_ARG);
 assert(wifi_deauther_send_capture_frame(ap,1)==ESP_ERR_INVALID_STATE);
 assert(sent_frames==0);
 get_result=ESP_FAIL;assert(wifi_deauther_send_capture_frame(ap,6)==ESP_FAIL);
 get_result=ESP_OK;s_is_running=true;
 assert(wifi_deauther_send_capture_frame(ap,6)==ESP_ERR_INVALID_STATE);
 s_is_running=false;
 assert(wifi_deauther_send_capture_frame(ap,6)==ESP_OK && sent_frames==1);
 for(unsigned i=4;i<10;i++)assert(sent_frame[i]==255);
 assert(memcmp(sent_frame+10,ap,6)==0 && memcmp(sent_frame+16,ap,6)==0);
 send_result=ESP_FAIL;
 assert(wifi_deauther_send_capture_frame(ap,6)==ESP_FAIL && sent_frames==2);
}
"""
with tempfile.TemporaryDirectory(prefix="capture-hopping-") as tmp:
    path=Path(tmp); c=path/"test.c"; binary=path/"test"
    c.write_text(source.split("#include",1)[0]+fakes+radio_fakes+radio_defines+"\n"+radio_template+radio_code+radio_tests+defines+"\n"+types+state+code+tests)
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                    str(c),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("PASS: scheduling, RSN/WPA bounds and per-BSSID observations, deferred one-shot TX, failures, per-BSSID budget, cancellation and holds")
