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

"""Fault-inject the production handshake worker lifecycle."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
s = (ROOT / "firmware_p4/components/Applications/ui/screens/wifi/wifi_handshake_ui.c").read_text()
start = s.index("static void poll_worker(")
end = s.index("static void screen_deleted(", start)
code = s[start:end]
callback = s[s.index("static void capture_stopped_cb("):s.index("static void state_ready_cb(")]
handler = s[s.index("static void wifi_handshake_input("):s.index("void ui_wifi_handshake_open(")]
code += handler
fakes = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define WIFI_SNIFFER_TYPE_EAPOL 2
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int s_snapshot_lock;
typedef struct { bool handshake_captured, pmkid_captured; } wifi_sniffer_stats_t;
static bool stats_ok=true;
static int flush_result;
static void led_signal_error(void) {}
static void publish_snapshot(const wifi_sniffer_stats_t *s) {(void)s;}
static bool wifi_sniffer_get_stats(wifi_sniffer_stats_t *s) { memset(s,0,sizeof(*s)); return stats_ok; }
static int wifi_sniffer_flush_capture(void) { return flush_result; }
#define MONITOR_CHANNEL 0
#define FLAG_HANDSHAKE 1u
#define FLAG_PMKID 2u
#define FLAG_FAILED 4u
#define GENERATION_STEP 8u
#define POLL_INTERVAL_MS 1000
#define pdMS_TO_TICKS(v) (v)
#define ESP_LOGE(...) ((void)0)
static atomic_bool s_is_poll_running,s_is_worker_busy;
static atomic_uint s_generation;
static uint8_t s_hs_bssid[6],s_pm_bssid[6];
static bool can_start;
static unsigned starts,stops,deleted,updates,last_update;
static void wifi_service_start(void) {}
static bool wifi_sniffer_start(int type, int ch) {assert(type==2);(void)ch;starts++;return can_start;}
static void wifi_sniffer_get_handshake_bssid(uint8_t *p) {(void)p;}
static void wifi_sniffer_get_pmkid_bssid(uint8_t *p) {(void)p;}
static void wifi_sniffer_stop(void) {stops++;}
#define SCREEN_WIFI_HANDSHAKE 1
#define SCREEN_WIFI_MENU 2
#define INPUT_ACTION_PRESS 1
#define INPUT_BTN_BACK 1
#define INPUT_BTN_UP 2
#define INPUT_BTN_DOWN 3
#define SCROLL_STEP 60
static unsigned screen=SCREEN_WIFI_HANDSHAKE,navigations;
static bool s_is_exit_requested;
static atomic_uint s_worker_generation;
static int s_scroll;
static void *s_status;
typedef struct {int action,button;} input_event_t;
static unsigned ui_current_screen(void) {return screen;}
static void ui_switch_screen(unsigned value) {screen=value;navigations++;}
static void lv_label_set_text(void *p,const char *t) {(void)p;assert(strstr(t,"Finishing"));}
static void menu_component_scroll_content(void *p,int delta) {(void)p;(void)delta;}
static void capture_stopped_cb(void *p);
static void state_ready_cb(void *p) {(void)p;}
static void ui_async_call(void (*cb)(void *),void *p) {
 if(cb==capture_stopped_cb)return;updates++;last_update=(unsigned)(uintptr_t)p;
}
static void vTaskDelay(unsigned ms) {assert(ms==1000);s_is_poll_running=false;}
static void vTaskDeleteWithCaps(void *p) {assert(!p);deleted++;}
"""
tests = r"""
int main(void) {
 s_generation=8;s_is_poll_running=true;s_is_worker_busy=true;
 poll_worker((void *)8);
 assert(starts==1 && stops==0 && deleted==1 && !s_is_worker_busy);
 assert(updates==1 && last_update==(8|FLAG_FAILED));
 can_start=true;s_is_poll_running=true;s_is_worker_busy=true;
 poll_worker((void *)8);
 assert(starts==2 && stops==1 && deleted==2 && !s_is_worker_busy);
 assert(last_update==8);
 s_is_poll_running=true;s_is_worker_busy=true;
 ui_wifi_handshake_stop();
 assert(s_generation==16 && !s_is_poll_running);
 poll_worker((void *)8);
 assert(starts==2 && stops==1 && deleted==3 && !s_is_worker_busy);
 s_generation=24;s_is_poll_running=true;s_is_worker_busy=true;
 flush_result=-1;
 poll_worker((void *)24);
 assert(stops==2 && last_update==(24|FLAG_FAILED) && !s_is_worker_busy);
 flush_result=0;stats_ok=false;s_is_poll_running=true;s_is_worker_busy=true;
 poll_worker((void *)24);
 assert(stops==3 && last_update==(24|FLAG_FAILED) && !s_is_worker_busy);
 s_generation=32;s_worker_generation=32;s_is_worker_busy=true;s_is_poll_running=true;
 input_event_t back={INPUT_ACTION_PRESS,INPUT_BTN_BACK};wifi_handshake_input(&back,NULL);
 assert(!s_is_poll_running && s_is_exit_requested && navigations==0 && s_generation==32);
 capture_stopped_cb((void *)24);assert(navigations==0);
 s_is_worker_busy=false;capture_stopped_cb((void *)32);assert(navigations==1 && screen==SCREEN_WIFI_MENU);
 puts("PASS: Back waits for drain, stale callback ignored; failed start, successful stop and cancellation before start");
}
"""
with tempfile.TemporaryDirectory(prefix="handshake-lifecycle-") as tmp:
    path = Path(tmp)
    c = path / "test.c"
    c.write_text(fakes + callback + code + tests)
    binary = path / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
