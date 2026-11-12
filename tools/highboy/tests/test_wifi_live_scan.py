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

"""Exercise production live scan with mocked SPI/FreeRTOS and sanitizers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "firmware_p4/components/Applications/wifi"
NOTICE = (SOURCE / "wifi_live_scan.c").read_text().split("#include")[0]
COMMON = r"""
#ifndef LIVE_TEST_FAKES_H
#define LIVE_TEST_FAKES_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define EXT_RAM_BSS_ATTR
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int portMUX_TYPE;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define SYS_CORE_RADIO 0
#define SYS_PRIO_SERVICE_LO 4
#define WIFI_SCAN_LIST_SIZE 64
typedef struct { uint8_t ssid[33], bssid[6], primary; int8_t rssi; } wifi_ap_record_t;
static bool ap_scanner_save_snapshot(const wifi_ap_record_t *records, uint16_t count) {
 (void)records;(void)count;return true;
}
static void (*task_fn)(void *);
static bool allocation_fails;
static int64_t now_us;
static int delete_count;
static int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, int stack,
 void *arg, int priority, void *handle, int core) {
 (void)name;(void)stack;(void)arg;(void)handle;
 assert(priority==SYS_PRIO_SERVICE_LO && core==SYS_CORE_RADIO);
 task_fn=fn; return allocation_fails ? 0 : pdPASS;
}
static void vTaskDelay(int ms) { now_us += (int64_t)ms * 1000; }
static void vTaskDelete(void *task) { assert(task==NULL);delete_count++; }
static int64_t esp_timer_get_time(void) { return now_us; }
#include "spi_protocol.h"
static esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *payload, uint8_t len,
 spi_header_t *header, uint8_t *out, size_t capacity, uint32_t timeout);
#endif
"""
TEST = r"""
#include "wifi_live_scan.c"
#include <stdio.h>
static int mode, starts, polls, records, stop_after, step_polls;
static uint8_t first_slot, last_slot;
static esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *payload, uint8_t len,
 spi_header_t *header, uint8_t *out, size_t capacity, uint32_t timeout) {
 assert(timeout>0);
 if(id==SPI_ID_WIFI_SCAN_STEP) {
   assert(len==1);last_slot=*payload;if(starts==0)first_slot=last_slot;
   starts++;step_polls=0;
   if(mode==4) return ESP_ERR_INVALID_STATE;
   return ESP_OK;
 }
 if(id==SPI_ID_WIFI_SCAN_STEP_STATUS) {
   polls++;step_polls++;
   assert(capacity==sizeof(spi_wifi_scan_step_status_t));
   spi_wifi_scan_step_status_t status={0};
   if(mode==5) status.busy=1;
   if(mode==2 && step_polls<3) {status.busy=1;wifi_live_scan_stop();}
   status.count=(mode==0 || mode==1 || mode==6) ? 1 : 0;
   if(mode==7) status.count=WIFI_SCAN_LIST_SIZE+1;
   memcpy(out,&status,sizeof(status));header->length=sizeof(status);
   if(mode==3) header->length--;
   if(mode==8) status.error=ESP_ERR_TIMEOUT;
   if(mode==8) memcpy(out,&status,sizeof(status));
   if(stop_after && starts>=stop_after) wifi_live_scan_stop();
   return ESP_OK;
 }
 assert(id==SPI_ID_WIFI_SCAN_STEP_RECORD);
 assert(len==sizeof(uint16_t) && capacity==sizeof(wifi_ap_record_t));
 wifi_ap_record_t record={.bssid={1,2,3,4,5,6},.primary=1,.rssi=-40};
 strcpy((char*)record.ssid,"Network");
 if(mode==0 && starts==2) {record.ssid[0]=0;record.rssi=-60;}
 if(mode==0 && starts==3) {record.bssid[5]=7;record.ssid[0]=1;}
 if(mode==1) {record.bssid[4]=starts>>8;record.bssid[5]=starts;}
 memcpy(out,&record,sizeof(record));header->length=sizeof(record);records++;
 if(mode==6)header->length--;
 return ESP_OK;
}
static void reset(int next_mode, int after) {
 mode=next_mode;starts=polls=records=0;now_us=0;stop_after=after;allocation_fails=false;
}
static wifi_live_scan_status_t status(void) {
 wifi_live_scan_status_t result;wifi_live_scan_get_status(&result);return result;
}
static void run(void) {assert(task_fn);task_fn(NULL);assert(!status().is_running);}
int main(void) {
 assert(wifi_live_scan_start(3)==ESP_ERR_INVALID_ARG);
 reset(0,4);
 assert(wifi_live_scan_start(SPI_WIFI_BAND_2G)==ESP_OK);
 assert(wifi_live_scan_start(SPI_WIFI_BAND_DUAL)==ESP_ERR_INVALID_STATE);
 run();assert(status().error==ESP_OK && status().count==2 && records==3);
 wifi_ap_record_t record;
 assert(wifi_live_scan_get_record(0,&record)==ESP_OK);
 assert(!strcmp((char*)record.ssid,"Network") && record.rssi==-60);
 wifi_live_scan_observation_t obs;
 assert(wifi_live_scan_get_observation(0,&obs)==ESP_OK);
 assert(obs.samples==2 && obs.rssi_min==-60 && obs.rssi_max==-40 && obs.rssi_sum==-100);
 assert(wifi_live_scan_get_record(1,&record)==ESP_OK && record.ssid[0]=='?');
 assert(wifi_live_scan_get_record(2,&record)==ESP_ERR_NOT_FOUND);
 assert(wifi_live_scan_get_record(0,NULL)==ESP_ERR_INVALID_ARG);
 assert(wifi_live_scan_get_observation(0,NULL)==ESP_ERR_INVALID_ARG);
 assert(wifi_live_scan_get_observation(2,&obs)==ESP_ERR_NOT_FOUND);
 // Immediate Back starts no RPC and does not retain the previous session's results.
 reset(0,0);assert(wifi_live_scan_start(0)==ESP_OK);wifi_live_scan_stop();run();
 assert(starts==0 && status().count==0);
 // Stop during an in-flight channel drains completion and prevents the next channel.
 reset(2,0);assert(wifi_live_scan_start(0)==ESP_OK);run();assert(starts==1 && polls==3);
 // 5 GHz begins at its own bitmap; 2.4 GHz wraps without entering 5 GHz.
 reset(0,2);assert(wifi_live_scan_start(SPI_WIFI_BAND_5G)==ESP_OK);run();
 assert(first_slot==SPI_WIFI_SCAN_2G_SLOTS);
 reset(0,15);assert(wifi_live_scan_start(SPI_WIFI_BAND_2G)==ESP_OK);run();assert(last_slot==0);
 // Bounded cache retains existing entries on overflow and publishes the limit.
 reset(1,131);assert(wifi_live_scan_start(0)==ESP_OK);run();
 assert(status().count==WIFI_LIVE_SCAN_MAX_APS && status().is_full);
 for(int failure=3;failure<=8;failure++) {
   reset(failure,0);assert(wifi_live_scan_start(0)==ESP_OK);run();
   assert(status().error!=ESP_OK && starts==1 && status().count==0);
 }
 reset(0,0);allocation_fails=true;
 assert(wifi_live_scan_start(0)==ESP_ERR_NO_MEM && !status().is_running);
 puts("live scan: deduplication, samples, bands, cancel/drain, overflow, failures, re-entry passed");
}
"""
C5_TEST = r"""
#include "fakes.h"
#define SCAN_MUTEX_TIMEOUT_MS 1000
#define SCAN_STEP_DWELL_MS 120
#define WIFI_BAND_MODE_AUTO 0
#define WIFI_SCAN_TYPE_ACTIVE 0
#define WIFI_SCAN_TYPE_PASSIVE 1
#define pdTRUE 1
typedef int wifi_band_mode_t;
typedef struct {
 bool show_hidden;
 int scan_type;
 struct { struct { uint32_t min, max; } active; uint32_t passive; } scan_time;
 struct { uint16_t ghz_2_channels; uint32_t ghz_5_channels; } channel_bitmap;
} wifi_scan_config_t;
static void *s_mutex=(void*)1;
static bool s_promiscuous_active, s_is_active=true;
static void *s_hopper_task_handle;
static uint16_t s_stored_ap_count;
static wifi_ap_record_t s_stored_aps[WIFI_SCAN_LIST_SIZE];
static bool mutex_allowed=true;
static int releases, band=2, driver_error, result_error;
static wifi_scan_config_t observed;
static int xSemaphoreTake(void *m, int timeout) { (void)m;(void)timeout;return mutex_allowed; }
static void xSemaphoreGive(void *m) { assert(m);releases++; }
static void wifi_service_start(void) { s_is_active=true; }
static int esp_wifi_get_band_mode(int *out) { *out=band;return ESP_OK; }
static int esp_wifi_set_band_mode(int value) { band=value;return ESP_OK; }
static int esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) {
 assert(block); observed=*config; return driver_error;
}
static int esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records) {
 assert(*count==WIFI_SCAN_LIST_SIZE && records==s_stored_aps);
 *count=2;return result_error;
}
static void esp_wifi_clear_ap_list(void) {}
/*PRODUCTION*/
int main(void) {
 assert(wifi_service_scan_step(0)==ESP_OK && band==2 && releases==1);
 assert(s_stored_ap_count==2 && observed.scan_type==WIFI_SCAN_TYPE_ACTIVE);
 assert(observed.show_hidden && observed.channel_bitmap.ghz_2_channels==2);
 assert(observed.channel_bitmap.ghz_5_channels==1 && observed.scan_time.active.max==120);
 assert(wifi_service_scan_step(14)==ESP_OK && band==2);
 assert(observed.scan_type==WIFI_SCAN_TYPE_PASSIVE && observed.scan_time.passive==120);
 assert(observed.channel_bitmap.ghz_2_channels==1 && observed.channel_bitmap.ghz_5_channels==2);
 assert(wifi_service_scan_step(41)==ESP_OK && observed.channel_bitmap.ghz_5_channels==(1u<<28));
 assert(wifi_service_scan_step(42)==ESP_ERR_INVALID_ARG);
 driver_error=ESP_ERR_TIMEOUT;
 assert(wifi_service_scan_step(0)==ESP_ERR_TIMEOUT && band==2 && s_stored_ap_count==0);
 driver_error=0;result_error=ESP_ERR_INVALID_STATE;
 assert(wifi_service_scan_step(0)==ESP_ERR_INVALID_STATE && s_stored_ap_count==0);
 result_error=0;s_promiscuous_active=true;
 assert(wifi_service_scan_step(0)==ESP_ERR_INVALID_STATE && band==2);
 s_promiscuous_active=false;s_hopper_task_handle=(void*)1;
 assert(wifi_service_scan_step(0)==ESP_ERR_INVALID_STATE);
 s_hopper_task_handle=NULL;mutex_allowed=false;
 assert(wifi_service_scan_step(0)==ESP_ERR_TIMEOUT);
 s_mutex=NULL;
 assert(wifi_service_scan_step(0)==ESP_ERR_INVALID_STATE);
}
"""

with tempfile.TemporaryDirectory(prefix="hb-wifi-live-") as temp:
    path = Path(temp)
    (path / "freertos").mkdir()
    (path / "fakes.h").write_text(NOTICE + COMMON)
    for name in ("esp_err.h", "wifi_service.h", "esp_attr.h", "esp_log.h", "esp_timer.h",
                 "freertos/FreeRTOS.h", "freertos/task.h", "spi_bridge.h", "sys_prio.h", "ap_scanner.h"):
        (path / name).write_text(NOTICE + '#include "fakes.h"\n')
    (path / "esp_rom_crc.h").write_text(NOTICE + """#include <stdint.h>
static inline uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *p, uint32_t n) {
 (void)p;(void)n;return crc;
}
""")
    (path / "test.c").write_text(NOTICE + TEST)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", "-I" + str(path),
                    "-I" + str(SOURCE), "-I" + str(SOURCE / "include"),
                    "-I" + str(ROOT / "firmware_p4/components/Service/spi_bridge/include"),
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

    c5_source = (ROOT / "firmware_c5/components/Service/wifi/wifi_service.c").read_text()
    start = c5_source.index("esp_err_t wifi_service_scan_step(")
    end = c5_source.index("uint16_t wifi_service_get_ap_count", start)
    (path / "c5_test.c").write_text(NOTICE + C5_TEST.replace("/*PRODUCTION*/", c5_source[start:end]))
    subprocess.run(["cc", "-std=c11", "-fsanitize=address,undefined", "-g",
                    "-I" + str(path),
                    "-I" + str(ROOT / "firmware_c5/components/Service/spi_bridge/include"),
                    str(path / "c5_test.c"), "-o", str(path / "c5_test")], check=True)
    subprocess.run([str(path / "c5_test")], check=True)
    print("C5 channel scans: bitmap/band, passive 5 GHz, restore, errors and ownership passed")
