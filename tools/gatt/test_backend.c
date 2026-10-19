#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gatt_explorer.h"
#include "bluetooth_service.h"
#include "freertos/semphr.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"

static struct ble_npl_event *s_pending;
static struct ble_npl_callout *s_timer;
static int (*s_gap)(struct ble_gap_event *,void *);
static gatt_svc_cb_t s_svc;
static gatt_chr_cb_t s_chr;
static gatt_read_cb_t s_read;
static void *s_svc_arg,*s_chr_arg,*s_read_arg;
static int s_error,s_alloc_failure,s_timer_failure,s_calls,s_terminates;
static bool s_running=true;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {return s_alloc_failure?NULL:(void *)1;}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h,TickType_t t) {(void)t;assert(h);return pdTRUE;}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h) {assert(h);return pdTRUE;}
bool bluetooth_service_is_running(void) {return s_running;}
esp_err_t bluetooth_service_connect(const uint8_t *a,uint8_t t,int (*cb)(struct ble_gap_event *,void *)) {
 assert(a && t==1);s_gap=cb;++s_calls;return s_error==1?ESP_FAIL:ESP_OK;
}
int ble_gap_terminate(uint16_t h,uint8_t r) {assert(h==9 && r==19);++s_terminates;return 0;}
int ble_gap_conn_cancel(void) {return BLE_HS_EALREADY;}
void ble_npl_event_init(struct ble_npl_event *e,void (*cb)(struct ble_npl_event *),void *a) {assert(!a);e->callback=cb;}
void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *e) {assert(q);s_pending=e;}
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) {static struct ble_npl_eventq q;return &q;}
int ble_npl_callout_init(struct ble_npl_callout *c,struct ble_npl_eventq *q,void (*cb)(struct ble_npl_event *),void *a) {
 assert(q && !a);s_timer=c;c->event.callback=cb;return 0;
}
void ble_npl_event_deinit(struct ble_npl_event *e) {assert(e);}
void ble_npl_eventq_remove(struct ble_npl_eventq *q,struct ble_npl_event *e) {assert(q);if(s_pending==e)s_pending=NULL;}
void ble_npl_callout_deinit(struct ble_npl_callout *c) {assert(c==s_timer);}
int ble_npl_callout_reset(struct ble_npl_callout *c,uint32_t t) {assert(c==s_timer && (t==60000 || t==1000));return s_timer_failure;}
void ble_npl_callout_stop(struct ble_npl_callout *c) {assert(c==s_timer);}
uint32_t ble_npl_time_ms_to_ticks32(uint32_t v) {return v;}
int ble_gattc_disc_all_svcs(uint16_t h,gatt_svc_cb_t cb,void *a) {assert(h==9);s_svc=cb;s_svc_arg=a;return s_error==2?5:0;}
int ble_gattc_disc_all_chrs(uint16_t h,uint16_t start,uint16_t end,gatt_chr_cb_t cb,void *a) {
 assert(h==9 && start==1 && end==8);s_chr=cb;s_chr_arg=a;return s_error==3?5:0;
}
int ble_gattc_read(uint16_t h,uint16_t v,gatt_read_cb_t cb,void *a) {assert(h==9 && v==3);s_read=cb;s_read_arg=a;return s_error==4?5:0;}
int os_mbuf_copydata(struct os_mbuf *m,int o,int n,void *out) {assert(o==0 && n<=m->length);memcpy(out,m->data,n);return 0;}
static void pump(void) {assert(s_pending);struct ble_npl_event *e=s_pending;s_pending=NULL;e->callback(e);}
static spi_gatt_status_t status(void) {spi_gatt_status_t v;assert(gatt_explorer_get_status(&v)==ESP_OK);return v;}
static void connect_peer(void) {struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=9}};s_gap(&e,NULL);}
static void disconnect_peer(void) {struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={.conn_handle=9}}};s_gap(&e,NULL);}
static void discover(void) {
 struct ble_gatt_error ok={0},done={BLE_HS_EDONE};
 struct ble_gatt_svc svc={.start_handle=1,.end_handle=8,.uuid={.u16={.type=16,.value=0x180f}}};
 s_svc(9,&ok,&svc,s_svc_arg);s_svc(9,&done,NULL,s_svc_arg);
 struct ble_gatt_chr chr={.val_handle=3,.properties=2,.uuid={.u16={.type=16,.value=0x2a19}}};
 s_chr(9,&ok,&chr,s_chr_arg);s_chr(9,&done,NULL,s_chr_arg);
 assert(status().state==SPI_GATT_READY && status().count==2);
}
int main(void) {
 const uint8_t address[6]={1};
 assert(status().state==SPI_GATT_IDLE && !gatt_explorer_is_busy());
 assert(!gatt_explorer_start(NULL,1) && !gatt_explorer_start(address,4));
 s_alloc_failure=1;assert(!gatt_explorer_start(address,1));s_alloc_failure=0;
 assert(gatt_explorer_start(address,1));assert(gatt_explorer_is_busy());
 assert(!gatt_explorer_start(address,1));assert(gatt_explorer_stop()==ESP_OK);pump();
 assert(s_calls==0 && status().state==SPI_GATT_IDLE);
 s_timer_failure=1;assert(gatt_explorer_start(address,1));pump();assert(status().state==SPI_GATT_ERROR);s_timer_failure=0;
 s_error=1;assert(gatt_explorer_start(address,1));pump();assert(status().state==SPI_GATT_ERROR);s_error=0;
 assert(gatt_explorer_start(address,1));pump();connect_peer();discover();
 spi_gatt_record_t rec;assert(gatt_explorer_get_record(0,&rec)==ESP_OK && rec.kind==SPI_GATT_SERVICE && rec.uuid[0]==0x0f);
 assert(gatt_explorer_get_record(2,&rec)==ESP_ERR_INVALID_ARG);
 assert(gatt_explorer_read(1)==ESP_ERR_INVALID_STATE);
 assert(gatt_explorer_read(3)==ESP_OK);pump();assert(status().state==SPI_GATT_READING);
 struct ble_gatt_error denied={5};s_read(9,&denied,NULL,s_read_arg);assert(status().state==SPI_GATT_READY && status().error==5);
 assert(gatt_explorer_read(3)==ESP_OK);pump();struct ble_gatt_error ok={0};
 struct os_mbuf mb={.length=80,.data={42}};struct ble_gatt_attr value={.handle=3,.om=&mb};
 s_read(9,&ok,&value,s_read_arg);assert(status().value_length==64 && status().value[0]==42 && status().error==BLE_HS_EMSGSIZE);
 void *old_arg=s_svc_arg;
 assert(gatt_explorer_stop()==ESP_OK);pump();disconnect_peer();assert(!gatt_explorer_is_busy());
 assert(gatt_explorer_start(address,1));pump();connect_peer();
 s_svc(9,&denied,NULL,old_arg);assert(status().state==SPI_GATT_DISCOVERING);
 struct ble_gatt_error failed={6};s_svc(9,&failed,NULL,s_svc_arg);assert(status().state==SPI_GATT_STOPPING);disconnect_peer();
 assert(status().state==SPI_GATT_ERROR && status().error==6);
 assert(gatt_explorer_start(address,1));pump();connect_peer();discover();
 s_timer->event.callback(&s_timer->event);assert(status().state==SPI_GATT_STOPPING);disconnect_peer();assert(status().error==BLE_HS_ETIMEOUT);
 assert(s_terminates==3);
 assert(gatt_explorer_deinit()==ESP_OK);
 assert(gatt_explorer_start(address,1));pump();connect_peer();discover();
 assert(gatt_explorer_deinit()==ESP_ERR_INVALID_STATE);
 assert(gatt_explorer_stop()==ESP_OK);pump();disconnect_peer();
 assert(gatt_explorer_deinit()==ESP_OK);
 assert(gatt_explorer_start(address,1));pump();connect_peer();
 struct ble_gatt_svc extra={.start_handle=1,.end_handle=8,.uuid={.u128={.type=128,.value={0xab}}}};
 for(unsigned i=0;i<SPI_GATT_RECORD_MAX+1;++i)s_svc(9,&ok,&extra,s_svc_arg);
 assert(status().count==SPI_GATT_RECORD_MAX && status().is_truncated);
 assert(gatt_explorer_get_record(0,&rec)==ESP_OK && rec.uuid_type==128 && rec.uuid[0]==0xab);
 struct ble_gatt_error done={BLE_HS_EDONE};s_svc(9,&done,NULL,s_svc_arg);
 for(unsigned i=0;i<SPI_GATT_RECORD_MAX;++i)s_chr(9,&done,NULL,s_chr_arg);
 assert(status().state==SPI_GATT_READY);
 assert(gatt_explorer_stop()==ESP_OK);pump();disconnect_peer();
 assert(gatt_explorer_start(address,1));pump();connect_peer();
 struct ble_gatt_svc svc={.start_handle=1,.end_handle=8,.uuid={.u16={.type=16,.value=0x180f}}};
 s_svc(9,&ok,&svc,s_svc_arg);s_svc(9,&done,NULL,s_svc_arg);
 s_chr(9,&denied,NULL,s_chr_arg);assert(status().state==SPI_GATT_STOPPING);disconnect_peer();
 assert(status().state==SPI_GATT_ERROR && status().error==5);
 assert(gatt_explorer_deinit()==ESP_OK);
 puts("PASS: real GATT discovery/read, lease, cancellation, stale callbacks, allocation and error paths; explicit NimBLE fakes");
 return 0;
}
