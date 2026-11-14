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

"""Run the production C5 pair/PMKID detector with malformed and unrelated traffic."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
s = (ROOT / "firmware_c5/components/Applications/wifi/wifi_sniffer.c").read_text()
def function(name):
    import re
    match = list(re.finditer(r"static (?:void|bool|esp_err_t) " + name + r"\([^;]+?\) \{", s, re.S))[-1]
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        depth += (s[pos] == "{") - (s[pos] == "}")
        pos += 1
    return s[start:pos]
macros = "\n".join(line for line in s.splitlines() if line.startswith("#define "))
struct_start = s.index("typedef struct {\n  uint8_t bssid")
struct_end = s.index("} sniffer_handshake_session_t;", struct_start) + len("} sniffer_handshake_session_t;")
fakes = r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM -1
#define ESP_LOGE(...) ((void)0)
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

static int64_t now_us;
static int64_t esp_timer_get_time(void) { return now_us; }
static bool s_is_handshake_captured, s_is_pmkid_captured;
static uint8_t s_handshake_bssid[6], s_pmkid_bssid[6];
static unsigned marked_count;
static uint8_t last_marked[6];
static void wifi_service_mark_captured(const uint8_t *bssid) {
 marked_count++;memcpy(last_marked,bssid,6);
}
"""
ap_start = s.index("typedef struct {\n  uint8_t bssid", struct_end)
ap_end = s.index("} sniffer_ap_info_t;", ap_start) + len("} sniffer_ap_info_t;")
tests = r"""
static void clear(void) {
 memset(s_sessions,0,MAX_TRACKED_SESSIONS*sizeof(*s_sessions));
 s_is_handshake_captured=false;s_is_pmkid_captured=false;now_us=0;marked_count=0;
}
int main(void) {
 fail_at=1;assert(prepare_tracking()==ESP_ERR_NO_MEM);
 assert(!s_sessions && !s_known_aps && live_allocations==0);
 allocations=0;fail_at=2;assert(prepare_tracking()==ESP_ERR_NO_MEM);
 assert(!s_sessions && !s_known_aps && live_allocations==0);
 allocations=0;fail_at=0;assert(prepare_tracking()==ESP_OK);
 assert(s_sessions && s_known_aps && live_allocations==2);
 s_known_aps[MAX_KNOWN_APS-1].has_ssid=true;
 assert(prepare_tracking()==ESP_OK && allocations==2);
 assert(s_known_aps[MAX_KNOWN_APS-1].has_ssid);

 uint8_t ap[6]={2}, sta[6]={4}, other[6]={6};
 uint8_t m1[99]={0},m2[99]={0},m3[99]={0},m4[99]={0};
 m1[4]=m2[4]=m3[4]=m4[4]=2;
 m1[6]=0x8a;m1[16]=7;m1[17]=1;
 m2[5]=1;m2[6]=0x0a;m2[16]=7;m2[17]=2;
 m3[5]=3;m3[6]=0xca;m3[16]=8;m3[17]=1;
 m4[5]=3;m4[6]=0x0a;m4[16]=8;
 uint8_t *messages[]={m1,m2,m3,m4};
 for(unsigned first=0;first<3;first++) {
  for(unsigned reverse=0;reverse<2;reverse++) {
   clear();unsigned a=first+reverse,b=first+1-reverse;
   track_handshake(ap,sta,messages[a]);assert(!s_is_handshake_captured);
   track_handshake(ap,sta,messages[b]);assert(s_is_handshake_captured && marked_count==1);
   track_handshake(other,sta,messages[a]);track_handshake(other,sta,messages[b]);
   assert(marked_count==2 && memcmp(last_marked,other,6)==0);
  }
  clear();track_handshake(ap,sta,messages[first]);
  track_handshake(ap,other,messages[first+1]);assert(!s_is_handshake_captured);
  clear();track_handshake(ap,sta,messages[first]);
  track_handshake(other,sta,messages[first+1]);assert(!s_is_handshake_captured);
  clear();track_handshake(ap,sta,messages[first]);now_us=11000000;
  track_handshake(ap,sta,messages[first+1]);assert(!s_is_handshake_captured);
  clear();track_handshake(ap,sta,messages[first]);
  messages[first+1][16]++;track_handshake(ap,sta,messages[first+1]);
  assert(!s_is_handshake_captured);messages[first+1][16]--;
  clear();track_handshake(ap,sta,messages[first]);
  messages[first+1][6]^=1;track_handshake(ap,sta,messages[first+1]);
  assert(!s_is_handshake_captured);messages[first+1][6]^=1;
  clear();track_handshake(ap,sta,messages[first]);
  messages[first+1][4]=254;track_handshake(ap,sta,messages[first+1]);
  assert(!s_is_handshake_captured);messages[first+1][4]=2;
 }
 clear();track_handshake(ap,sta,m1);track_handshake(ap,sta,m3);assert(!s_is_handshake_captured);
 clear();track_handshake(ap,sta,m2);track_handshake(ap,sta,m4);assert(!s_is_handshake_captured);
 clear();m2[17]=0;track_handshake(ap,sta,m1);track_handshake(ap,sta,m2);
 assert(!s_is_handshake_captured);m2[17]=2;
 clear();m4[17]=2;track_handshake(ap,sta,m3);track_handshake(ap,sta,m4);
 assert(!s_is_handshake_captured);m4[17]=0;
 clear();m4[98]=1;track_handshake(ap,sta,m3);track_handshake(ap,sta,m4);
 assert(!s_is_handshake_captured);m4[98]=0;
 for(unsigned i=0;i<4;i++) {
  clear();messages[i][5]|=4;track_handshake(ap,sta,messages[i]);
  assert(!s_is_handshake_captured);messages[i][5]&=~4;
 }
 // Legacy WPA M4 may omit Secure; RSN M4 cannot.
 clear();m3[4]=m4[4]=254;m4[5]=1;
 track_handshake(ap,sta,m3);track_handshake(ap,sta,m4);assert(s_is_handshake_captured);
 clear();m3[4]=m4[4]=2;
 track_handshake(ap,sta,m3);track_handshake(ap,sta,m4);assert(!s_is_handshake_captured);
 m4[5]=3;
 // Full 64-bit replay increment, including carry; overflow cannot join sessions.
 uint8_t before[8]={0,0,0,0,0,0,0,255},after[8]={0,0,0,0,0,0,1,0};
 assert(replay_follows(before,after));memset(before,255,8);memset(after,0,8);
 assert(!replay_follows(before,after));
 clear();now_us=4294967290000LL;track_handshake(ap,sta,m2);now_us+=20000;
 track_handshake(ap,sta,m3);assert(s_is_handshake_captured);
 // Unrelated sessions do not collide before the bounded table fills.
 clear();for(unsigned i=0;i<16;i++) {uint8_t client[6]={4,0,0,0,0,(uint8_t)i};
  track_handshake(ap,client,m3);}
 for(unsigned i=0;i<16;i++) {uint8_t client[6]={4,0,0,0,0,(uint8_t)i};
  track_handshake(ap,client,m4);}
 assert(marked_count==16);
 clear();
 uint8_t packet[129]={0};packet[9]=3;packet[105]=0;packet[106]=22;
 packet[107]=221;packet[108]=20;packet[109]=0;packet[110]=0x0f;packet[111]=0xac;packet[112]=4;
 packet[113]=1;
 for(int len=0;len<129;len++) assert(!check_pmkid_presence(packet,len,ap));
 assert(check_pmkid_presence(packet,sizeof(packet),ap));
 assert(marked_count==1 && memcmp(last_marked,ap,6)==0);
 assert(check_pmkid_presence(packet,sizeof(packet),other));
 assert(marked_count==2 && memcmp(last_marked,other,6)==0);
 uint8_t rsn_packet[147]={0};rsn_packet[9]=3;rsn_packet[106]=40;
 rsn_packet[107]=48;rsn_packet[108]=38;rsn_packet[109]=1;
 rsn_packet[115]=1;rsn_packet[121]=1;rsn_packet[129]=1;rsn_packet[131]=1;
 assert(check_pmkid_presence(rsn_packet,sizeof(rsn_packet),ap));
 assert(marked_count==3 && memcmp(last_marked,ap,6)==0);
 assert(check_pmkid_presence(rsn_packet,sizeof(rsn_packet),other));
 assert(marked_count==4 && memcmp(last_marked,other,6)==0);
 packet[108]=255;assert(!check_pmkid_presence(packet,sizeof(packet),ap));
 now_us=1000000;memcpy(s_known_aps[0].bssid,ap,6);
 assert(should_save_metadata(ap,BEACON_SUBTYPE));
 assert(!should_save_metadata(ap,BEACON_SUBTYPE));
 assert(should_save_metadata(ap,PROBE_RESP_SUBTYPE));
 assert(!should_save_metadata(ap,PROBE_RESP_SUBTYPE));
 now_us+=5000000;assert(should_save_metadata(ap,BEACON_SUBTYPE));
 assert(should_save_metadata(other,ASSOC_REQ_SUBTYPE));
 assert(!should_save_metadata(other,BEACON_SUBTYPE));
 puts("PASS: all three EAPOL pairs in both orders, AP/client isolation, replay, expiry/wrap, key version/descriptor, nonce, PMKID and metadata");
}
"""
with tempfile.TemporaryDirectory(prefix="handshake-parser-") as tmp:
    path=Path(tmp)
    c=path/"test.c"
    c.write_text(s.split("#include",1)[0]+fakes+macros+"\n"+s[struct_start:struct_end]+"\nstatic sniffer_handshake_session_t *s_sessions;\n"+s[ap_start:ap_end]+"\nstatic sniffer_ap_info_t *s_known_aps;\n"+function("prepare_tracking")+function("should_save_metadata")+function("replay_follows")+function("track_handshake")+function("check_pmkid_presence")+tests)
    binary=path/"test"
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-sanitize-recover=all",str(c),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
