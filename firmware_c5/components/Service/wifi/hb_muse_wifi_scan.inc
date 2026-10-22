// SPDX-License-Identifier: GPL-3.0-or-later
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

/* Native phone discovery, executed only by the exclusive setup owner. Uses
 * the existing Wi-Fi mutex/radio; never starts a new radio or changes its band.
 * A cancelled/uncertain scan is not retried in this boot. */
#include "hb_muse_pairing.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdatomic.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#define SCAN_TRACE(stage, code) ESP_LOGI("MUSE_SCAN", "stage=%s code=%d", stage, (int)(code))
#else
#define SCAN_TRACE(stage, code) do { (void)(stage); (void)(code); } while (0)
#endif
static esp_event_base_t const MUSE_SCAN_BARRIER="muse_scan_barrier";
static atomic_bool s_muse_scan_barrier,s_muse_scan_armed,s_muse_scan_done;
static atomic_uint s_muse_scan_result;
static bool s_muse_scan_fault;
static void muse_scan_event(void *user,esp_event_base_t base,int32_t id,void *data) {
    (void)user;
    if(base==MUSE_SCAN_BARRIER && id==0) atomic_store(&s_muse_scan_barrier,true);
    else if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE && data && atomic_load(&s_muse_scan_armed)) {
        wifi_event_sta_scan_done_t *event=data;
        atomic_store(&s_muse_scan_result,event->status);
        atomic_store(&s_muse_scan_done,true);
    }
}
static bool muse_scan_current(bool (*guard)(void *),void *user,int64_t start,int64_t *last) {
    int64_t now=esp_timer_get_time();
    bool ok=now>=*last && now>=start && now-start<(int64_t)HB_MUSE_PAIRING_SCAN_TIMEOUT_MS*1000;
    *last=now;
    if(!ok) { SCAN_TRACE("deadline_or_clock", 1); return false; }
    if(!guard(user)) { SCAN_TRACE("session_guard", 1); return false; }
    return true;
}
bool hb_muse_wifi_scan_guarded(hb_muse_pairing_network_t *out,size_t capacity,size_t *count,
                               bool (*guard)(void *),void *user) {
    if(count) *count=0;
    if(!out || !count || !guard || !capacity || capacity>16 || !s_mutex || !s_is_active ||
       s_promiscuous_active || s_hopper_task_handle || !guard(user)) {
        SCAN_TRACE("precondition", (!s_mutex?1:0)|(!s_is_active?2:0)|
                   (s_promiscuous_active?4:0)|(s_hopper_task_handle?8:0));
        return false;
    }
    memset(out,0,capacity*sizeof(*out));
    if(xSemaphoreTake(s_mutex,0)!=pdTRUE) { SCAN_TRACE("mutex_busy",1); return false; }
    bool ok=false,started=false;
    const char *stage="initial_guard";
    int driver_error=0;
    esp_event_handler_instance_t event_instance=NULL,barrier_instance=NULL;
    wifi_ap_record_t *records=NULL;
    int64_t start=esp_timer_get_time(),last=start;
    if(s_muse_scan_fault) { stage="previous_scan_fault_reboot_required"; goto done; }
    if(start<0 || !muse_scan_current(guard,user,start,&last)) goto done;
    stage="records_allocate";
    records=heap_caps_calloc(capacity,sizeof(*records),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!records) goto done;
    atomic_store(&s_muse_scan_barrier,false); atomic_store(&s_muse_scan_armed,false);
    atomic_store(&s_muse_scan_done,false); atomic_store(&s_muse_scan_result,1u);
    stage="register_scan_event";
    if((driver_error=esp_event_handler_instance_register(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,muse_scan_event,NULL,&event_instance))!=ESP_OK) goto done;
    stage="register_barrier";
    if((driver_error=esp_event_handler_instance_register(MUSE_SCAN_BARRIER,0,muse_scan_event,NULL,&barrier_instance))!=ESP_OK) goto done;
    stage="post_barrier";
    if((driver_error=esp_event_post(MUSE_SCAN_BARRIER,0,NULL,0,0))!=ESP_OK) goto done;
    stage="wait_barrier";
    /* Drain previously queued events before arming. Legacy scans are blocking
     * and use this same mutex; the SPI owner denies concurrent scan requests. */
    while(!atomic_load(&s_muse_scan_barrier)) {
        if(!muse_scan_current(guard,user,start,&last)) goto done;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    stage="clear_previous_results";
    if(!muse_scan_current(guard,user,start,&last) || (driver_error=esp_wifi_clear_ap_list())!=ESP_OK) goto done;
    wifi_scan_config_t config={.ssid=NULL,.bssid=NULL,.channel=0,.show_hidden=false,
        /* IDF default active timings, required for BLE coexistence. Keep
         * cancellation and the original setup-window guard throughout. */
        .scan_type=WIFI_SCAN_TYPE_ACTIVE,.scan_time.active={.min=0,.max=120}};
    atomic_store(&s_muse_scan_armed,true);
    stage="driver_start";
    if((driver_error=esp_wifi_scan_start(&config,false))!=ESP_OK) goto done;
    SCAN_TRACE("driver_started",0);
    stage="wait_scan_event";
    started=true;
    while(!atomic_load(&s_muse_scan_done)) {
        if(!muse_scan_current(guard,user,start,&last)) goto done;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    stage="scan_event_status";
    SCAN_TRACE("event_received_elapsed_ms",(esp_timer_get_time()-start)/1000);
    driver_error=(int)atomic_load(&s_muse_scan_result);
    if(driver_error!=0 || !muse_scan_current(guard,user,start,&last)) goto done;
    uint16_t n=(uint16_t)capacity;
    stage="read_results";
    if((driver_error=esp_wifi_scan_get_ap_records(&n,records))!=ESP_OK || n>capacity ||
       !muse_scan_current(guard,user,start,&last)) goto done;
    for(unsigned i=0;i<n;++i) {
        size_t length=strnlen((const char *)records[i].ssid,sizeof(records[i].ssid));
        bool printable=length>0 && length<=32;
        for(size_t j=0;j<length;++j) if(records[i].ssid[j]<0x20 || records[i].ssid[j]>0x7e) printable=false;
        if(!printable) continue; /* unsupported names are omitted, never renamed/truncated */
        hb_muse_pairing_network_t *network=&out[(*count)++];
        memcpy(network->ssid,records[i].ssid,length); network->ssid[length]=0;
        network->rssi=records[i].rssi; network->secure=records[i].authmode!=WIFI_AUTH_OPEN;
    }
    ok=muse_scan_current(guard,user,start,&last);
done:
    SCAN_TRACE(ok?"scan_complete":stage,driver_error);
    SCAN_TRACE("elapsed_ms",(esp_timer_get_time()-start)/1000);
    atomic_store(&s_muse_scan_armed,false);
    if(started && !ok) { s_muse_scan_fault=true; (void)esp_wifi_scan_stop(); }
    if(started) (void)esp_wifi_clear_ap_list();
    if(event_instance && esp_event_handler_instance_unregister(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,event_instance)!=ESP_OK)
        { s_muse_scan_fault=true; ok=false; }
    if(barrier_instance && esp_event_handler_instance_unregister(MUSE_SCAN_BARRIER,0,barrier_instance)!=ESP_OK)
        { s_muse_scan_fault=true; ok=false; }
    if(records) { memset(records,0,capacity*sizeof(*records)); heap_caps_free(records); }
    if(!ok) { memset(out,0,capacity*sizeof(*out)); *count=0; }
    xSemaphoreGive(s_mutex); return ok;
}
