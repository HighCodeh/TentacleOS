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

"""Exercise the real per-AP writer against files, with malformed frames and restart."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
APP=ROOT/"firmware_p4/components/Applications/wifi"
HEADERS={
"led_control.h": "extern unsigned test_flashes;\nstatic inline void led_signal_info(void) {test_flashes++;}\n",
"esp_err.h": "typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_STATE -2\n#define ESP_ERR_NO_MEM -3\n#define ESP_ERR_INVALID_RESPONSE -4\n#define ESP_ERR_INVALID_SIZE -5\n#define ESP_ERR_INVALID_ARG -6\n",
"esp_heap_caps.h": "#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define heap_caps_calloc(n,s,c) calloc(n,s)\n",
"esp_log.h": "#define ESP_LOGE(tag,...) ((void)(tag))\n#define ESP_LOGI(tag,...) ((void)(tag))\n",
"storage_init.h": "#include <stdbool.h>\nstatic inline bool storage_is_mounted(void) {return true;}\n",
"storage_mkdir.h": "#include <sys/stat.h>\n#include <errno.h>\n#include \"esp_err.h\"\nstatic inline int storage_mkdir_recursive(const char *p) {return mkdir(p,0700)==0||errno==EEXIST?0:-1;}\n",
"tos_storage_paths.h": "extern const char *test_dir;\n#define TOS_PATH_WIFI_LOOT_HS test_dir\n",
}
HARNESS=r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "wifi_ap_capture.h"
#include "pcap_serializer.h"
const char *test_dir;
unsigned test_flashes;
static uint8_t client[6]={0x10,0x20,0x30,0x40,0x50,0x60};
static void beacon(uint8_t *f,size_t *len,const uint8_t *ap,const char *ssid) {
 memset(f,0,256);f[0]=0x80;memset(f+4,0xff,6);memcpy(f+10,ap,6);memcpy(f+16,ap,6);
 f[32]=100;f[34]=0x11;f[36]=0;f[37]=strlen(ssid);memcpy(f+38,ssid,strlen(ssid));
 *len=38+strlen(ssid)+4;
}
static void eapol(uint8_t *f,size_t *len,const uint8_t *ap,int message) {
 memset(f,0,256);f[0]=8;f[1]=message==1?2:1;
 memcpy(f+4,message==1?client:ap,6);memcpy(f+10,message==1?ap:client,6);memcpy(f+16,ap,6);
 uint8_t llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};memcpy(f+24,llc,8);
 uint8_t *e=f+32;e[0]=2;e[1]=3;e[3]=95;e[4]=2;
 e[5]=message==1?0:1;e[6]=message==1?0x8a:0x0a;e[8]=16;e[16]=7;
 memset(e+17,message,32);if(message==2)memset(e+81,0x55,16);*len=135;
}
static size_t count_records(const char *path,const uint8_t *ap) {
 FILE *f=fopen(path,"rb");assert(f);pcap_global_header_t h;assert(fread(&h,1,sizeof(h),f)==sizeof(h));
 assert(h.magic_number==PCAP_MAGIC_NUMBER && h.network==105);
 pcap_packet_header_t r;uint8_t frame[2048];size_t count=0;
 while(fread(&r,1,sizeof(r),f)==sizeof(r)) {
  assert(r.incl_len<=sizeof(frame));assert(fread(frame,1,r.incl_len,f)==r.incl_len);
  assert(!memcmp(frame+16,ap,6));count++;
 }
 fclose(f);return count;
}
int main(int argc,char **argv) {
 assert(argc==2);test_dir=argv[1];assert(wifi_ap_capture_start()==0);
 uint8_t ap1[]={0xaa,0xbb,0xcc,0xdd,0xee,1},ap2[]={0xaa,0xbb,0xcc,0xdd,0xee,2};
 uint8_t f[256];size_t len;char path1[512],path2[512],hidden[512];
 snprintf(path1,sizeof(path1),"%s/Minha_Rede_AABBCCDDEE01.pcap",test_dir);
 snprintf(path2,sizeof(path2),"%s/Minha_Rede_AABBCCDDEE02.pcap",test_dir);
 snprintf(hidden,sizeof(hidden),"%s/hidden_AABBCCDDEE02.pcap",test_dir);
 struct timeval received={123456789,12345};
 beacon(f,&len,ap1,"Minha Rede");assert(wifi_ap_capture_frame_at(f,len,&received)==0);
 wifi_ap_capture_snapshot_t snapshot;
 wifi_ap_capture_snapshot(&snapshot);assert(snapshot.count==0 && test_flashes==0);
 struct stat st;assert(stat(path1,&st)!=0);
 received.tv_usec=54321;
 eapol(f,&len,ap1,1);assert(wifi_ap_capture_frame_at(f,len,&received)==0);
 FILE *timed=fopen(path1,"rb");assert(timed);pcap_packet_header_t record;
 assert(fseek(timed,sizeof(pcap_global_header_t),SEEK_SET)==0);
 assert(fread(&record,1,sizeof(record),timed)==sizeof(record));
 assert(record.ts_sec==123456789 && record.ts_usec==12345);
 assert(fseek(timed,record.incl_len,SEEK_CUR)==0);
 assert(fread(&record,1,sizeof(record),timed)==sizeof(record));
 assert(record.ts_sec==123456789 && record.ts_usec==54321);fclose(timed);
 eapol(f,&len,ap1,2);assert(wifi_ap_capture_frame(f,len)==0);
 eapol(f,&len,ap2,1);assert(wifi_ap_capture_frame(f,len)==0);assert(stat(hidden,&st)==0);
 beacon(f,&len,ap2,"Minha Rede");assert(wifi_ap_capture_frame(f,len)==0);assert(stat(hidden,&st)!=0);
 eapol(f,&len,ap2,2);assert(wifi_ap_capture_frame(f,len)==0);
 assert(count_records(path1,ap1)==3);assert(count_records(path2,ap2)==3);
 wifi_ap_capture_snapshot(&snapshot);
 assert(snapshot.count==2 && snapshot.frames==4 && test_flashes==2);
 assert(snapshot.recent[0].has_ssid && snapshot.recent[0].frames==2);
 assert(strcmp(snapshot.recent[0].filename,"Minha_Rede_AABBCCDDEE02.pcap")==0);
 size_t before=wifi_ap_capture_bytes();assert(before>0);
 wifi_ap_capture_stop();assert(wifi_ap_capture_start()==0);
 beacon(f,&len,ap1,"Minha Rede");assert(wifi_ap_capture_frame(f,len)==0);
 eapol(f,&len,ap1,1);assert(wifi_ap_capture_frame(f,len)==0);
 assert(count_records(path1,ap1)==5);assert(count_records(path2,ap2)==3);
 for(size_t n=0;n<24;n++) assert(wifi_ap_capture_frame(f,n)==0);
 uint8_t ap3[]={0xaa,0xbb,0xcc,0xdd,0xee,3};
 beacon(f,&len,ap3,"../A:B/C");assert(wifi_ap_capture_frame(f,len)==0);
 eapol(f,&len,ap3,1);assert(wifi_ap_capture_frame(f,len)==0);
 wifi_ap_capture_stop();
 FILE *bad=fopen(path1,"ab");assert(bad);fputc(0xff,bad);fclose(bad);
 assert(wifi_ap_capture_start()==0);eapol(f,&len,ap1,1);
 unsigned flashes_before=test_flashes;
 assert(wifi_ap_capture_frame(f,len)==ESP_ERR_INVALID_RESPONSE);
 wifi_ap_capture_snapshot(&snapshot);assert(snapshot.count==0 && snapshot.frames==0);
 assert(test_flashes==flashes_before);
 wifi_ap_capture_stop();puts("PASS: isolated AP files, SSID metadata, late rename, append, safe names, malformed inputs");
}
"""
with tempfile.TemporaryDirectory(prefix="ap-capture-") as tmp:
    path=Path(tmp);inc=path/"include";inc.mkdir()
    for name,source in HEADERS.items():
        (inc/name).write_text(source)
    c=path/"test.c";c.write_text(HARNESS);binary=path/"test"
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-sanitize-recover=all","-I"+str(inc),"-I"+str(APP/"include"),str(APP/"wifi_ap_capture.c"),str(c),"-o",str(binary)],check=True)
    captures=path/"captures"
    subprocess.run([str(binary),str(captures)],check=True,timeout=30)
    names=sorted(p.name for p in captures.glob("*.pcap"))
    assert names==["Minha_Rede_AABBCCDDEE01.pcap","Minha_Rede_AABBCCDDEE02.pcap","___A_B_C_AABBCCDDEE03.pcap"],names
    if shutil.which("tshark"):
        result=subprocess.run(["tshark","-r",str(captures/"Minha_Rede_AABBCCDDEE02.pcap"),"-Y","eapol || wlan.ssid","-T","fields","-e","frame.protocols"],capture_output=True,text=True,check=True)
        assert result.stdout.count("eapol")==2,result.stdout
        ssid=subprocess.run(["tshark","-r",str(captures/"Minha_Rede_AABBCCDDEE02.pcap"),"-Y","wlan.ssid","-T","fields","-e","wlan.ssid"],capture_output=True,text=True,check=True).stdout.strip()
        assert ssid in ("Minha Rede",b"Minha Rede".hex()),ssid
        print("PASS: tshark reads the original SSID and two EAPOL frames")
