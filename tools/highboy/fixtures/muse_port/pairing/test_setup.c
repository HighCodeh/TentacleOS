/* Actual setup owner + fragment codec; explicit SDK/GATT/runtime/store fakes.
 * No NimBLE, network, radio, API, real credentials or target scheduler. */
#include "hb_muse_setup.h"
#include "hb_muse_pairing.h"
#include "hb_muse_pairing_gatt.h"
#include "../runtime/hb_muse_runtime.h"
#include "../platform/hb_muse_store.h"
#include "setup_host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t now_ms = 100;
static bool quiet = true, paired, store_read_ok = true, bt_ok = true, wifi_ok = true;
static bool revoke_wifi, revoke_commit, expire_wifi, notify_ok = true;
static unsigned acquire_calls, advertise_calls, release_calls, notify_calls, receive_calls, commit_calls, wifi_calls;
static unsigned lock_depth;
static bool revoke_delay;
static unsigned delays;
static hb_muse_pairing_config_t pair_ops;
static hb_muse_pairing_gatt_config_t gatt_ops;
static hb_muse_pairing_status_t pair_status;
static hb_muse_runtime_status_t runtime_status = {.phase=HB_MUSE_RUNTIME_DISABLED,.fenced=true};
void hb_setup_test_enter(portMUX_TYPE *lock) { assert(!lock->held); lock->held=1; ++lock_depth; }
void hb_setup_test_exit(portMUX_TYPE *lock) { assert(lock->held && lock_depth); lock->held=0; --lock_depth; }
void hb_setup_test_delay(unsigned milliseconds) {
    assert(!lock_depth && milliseconds==50); ++delays; now_ms += milliseconds;
    hb_muse_setup_status_t status; assert(hb_muse_setup_status(&status));
    if (revoke_delay) hb_muse_setup_revoke();
}
bool hb_muse_runtime_status(hb_muse_runtime_status_t *out) { assert(!lock_depth); *out=runtime_status; return true; }
void hb_muse_runtime_revoke(void) { assert(!lock_depth); runtime_status.cloud_authorized=false; }
bool hb_muse_runtime_set_enabled(bool enabled) { assert(!lock_depth); runtime_status.enabled=enabled; return true; }
bool hb_muse_store_load_settings(void *u,hb_muse_platform_settings_t *out) {
    (void)u; assert(!lock_depth); memset(out,0,sizeof(*out)); out->account_present=paired; return store_read_ok;
}
bool hb_muse_store_import_initial(const hb_muse_store_record_t *r,bool(*guard)(void*),void*u) {
    assert(!lock_depth && r->magic==HB_MUSE_STORE_MAGIC && r->version==1);
    assert(!strcmp(r->device_id,"fixture-node") && !strcmp(r->access,"fixture-access"));
    assert(!r->host[0] && !r->preferred_vm[0] && !r->preferred_url[0]);
    if(revoke_commit) hb_muse_setup_revoke();
    if(!guard(u)) return false;
    ++commit_calls; paired=true; return guard(u);
}
bool hb_muse_pairing_gatt_register(const hb_muse_pairing_gatt_config_t *c) { assert(!lock_depth);gatt_ops=*c;return bt_ok; }
void hb_muse_pairing_gatt_stack_reset(void) { memset(&gatt_ops,0,sizeof(gatt_ops)); }
bool hb_muse_pairing_gatt_notify(uint16_t handle,const uint8_t*d,size_t n) {
    assert(!lock_depth && d && n && n<=160 && handle==7); ++notify_calls;
    return notify_ok && gatt_ops.current(gatt_ops.user,handle);
}
bool link_pairing_record_session_is_current(uint32_t generation) { return generation==1; }
bool hb_muse_pairing_init(const hb_muse_pairing_config_t *c) { assert(!lock_depth);pair_ops=*c;assert(!strcmp(c->sdk_token,"mgst_fixture"));return true; }
bool hb_muse_pairing_open(uint64_t window,uint64_t now) { (void)now; pair_status=(hb_muse_pairing_status_t){.phase=HB_MUSE_PAIRING_OPEN,.window=window};return true; }
bool hb_muse_pairing_connected(uint64_t connection,uint64_t now) { (void)now;assert(pair_status.phase==HB_MUSE_PAIRING_OPEN);pair_status.connection=connection;return true; }
static bool guarded(void *unused) { (void)unused;return pair_ops.current(NULL,pair_status.window,pair_status.connection); }
bool hb_muse_pairing_receive(uint64_t connection,const uint8_t *json,size_t n,uint64_t now) {
    (void)now; assert(!lock_depth && connection==pair_status.connection); ++receive_calls;
    if(n==5 && !memcmp(json,"hello",5)) {
        pair_status.phase=HB_MUSE_PAIRING_CONFIRM;pair_status.confirmation_ticket=123;
        return pair_ops.send(NULL,connection,"{\"status\":\"fixture\"}",20,1);
    }
    if(n==5 && !memcmp(json,"store",5)) {
        hb_muse_pairing_credentials_t *c=calloc(1,sizeof(*c));assert(c);
        strcpy(c->ssid,"fixture-ssid");strcpy(c->password,"fixture-password");
        strcpy(c->access_token,"fixture-access");strcpy(c->refresh_token,"fixture-refresh");
        bool ok=pair_ops.wifi_connect(NULL,c,guarded,NULL) && pair_ops.store(NULL,c,guarded,NULL);free(c);
        if(ok) pair_status.phase=HB_MUSE_PAIRING_STORED;
        return ok;
    }
    return false;
}
bool hb_muse_pairing_button(uint64_t ticket,uint64_t edge,bool pressed,uint64_t now) {
    (void)now;(void)edge;
    assert(ticket==123);
    if(!pressed) pair_status.release_seen=true;
    else if(pair_status.release_seen) pair_status.phase=HB_MUSE_PAIRING_CONFIRMED;
    return true;
}
void hb_muse_pairing_poll(uint64_t now) { (void)now; }
void hb_muse_pairing_disconnect(uint64_t connection) { assert(connection==pair_status.connection);pair_status.phase=HB_MUSE_PAIRING_OPEN;pair_status.connection=0; }
void hb_muse_pairing_revoke(void) { pair_status=(hb_muse_pairing_status_t){0}; }
hb_muse_pairing_status_t hb_muse_pairing_status(void) { return pair_status; }
static uint64_t clock_ms(void*u) { (void)u;assert(!lock_depth);return now_ms; }
static bool acquire(void*u,bool(*register_service)(void)) { (void)u;assert(!lock_depth);++acquire_calls;return bt_ok && register_service(); }
static bool advertise(void*u) { (void)u;assert(!lock_depth);++advertise_calls;return bt_ok; }
static void release(void*u) { (void)u;assert(!lock_depth);++release_calls; }
static bool services(void*u) { (void)u;assert(!lock_depth);return quiet; }
static bool wifi(void*u,const char*s,const char*p,bool(*guard)(void*),void*g) {
    (void)u;assert(!lock_depth && !strcmp(s,"fixture-ssid") && !strcmp(p,"fixture-password"));
    if(revoke_wifi) hb_muse_setup_revoke();if(expire_wifi) now_ms+=120000;
    if(!guard(g)) return false;
    ++wifi_calls; return wifi_ok && guard(g);
}
static hb_muse_setup_status_t status(void) { hb_muse_setup_status_t s;assert(hb_muse_setup_status(&s));return s; }
static bool token_guard(void *allow) { assert(lock_depth==1);return *(bool*)allow; }
static void init(void) {
    const hb_muse_setup_config_t c={clock_ms,acquire,advertise,release,services,wifi,NULL,
        "fixture-node","fixture-device","00:11:22:33:44:55","fixture-version",NULL};
    assert(hb_muse_setup_init(&c));
}
static void open_setup(void) {
    assert(hb_muse_setup_set_local_token("mgst_fixture",12));assert(hb_muse_setup_open());
    hb_muse_setup_host_step();assert(status().phase==HB_MUSE_SETUP_ADVERTISING && advertise_calls==1);
}
static void phone(void) {
    open_setup();assert(hb_muse_setup_gap_connected(7,64));hb_muse_setup_gap_subscribed(7,true,64);
    hb_muse_setup_host_step();assert(status().phase==HB_MUSE_SETUP_PHONE);
}
static bool receive(const char *text) { return gatt_ops.defer_rx(NULL,7,(const uint8_t*)text,strlen(text)); }
static void confirm(void) {
    phone();assert(receive("hello"));hb_muse_setup_host_step();assert(status().phase==HB_MUSE_SETUP_CONFIRM);
    assert(hb_muse_setup_button(123,1,false));assert(hb_muse_setup_button(123,2,true));hb_muse_setup_host_step();
    assert(status().phase==HB_MUSE_SETUP_CONFIRMED);
}
int main(int argc,char**argv) {
    const char*cases[]={"token_required","token_bounds","normal","revoke_queue","overflow","disconnect_reuse",
        "expired_queue","runtime_fence","runtime_enable","store_error","already_paired","bt_failure",
        "wrong_ticket","unsubscribe","revoke_wifi","expire_wifi","revoke_commit","wifi_failure",
        "bad_fragment","clock_back","notify_failure","second_central","stack_reset","guarded_token",
        "delayed_subscription","notify_pacing_revoke","saved_phone_disconnect"};
    if(argc==1) {for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i)puts(cases[i]);return 0;}
    const char*t=argv[1];init();
    if(!strcmp(t,"token_required")) {assert(!hb_muse_setup_open());hb_muse_setup_host_step();assert(!acquire_calls && status().error==HB_MUSE_SETUP_TOKEN_REQUIRED);}
    else if(!strcmp(t,"token_bounds")) {
        assert(!hb_muse_setup_set_local_token("mgst_x\n",7));assert(!hb_muse_setup_set_local_token("mgst_",5));
        assert(hb_muse_setup_set_local_token("mgst_fixture",12));assert(!hb_muse_setup_set_local_token("mgst_other",10));
        assert(hb_muse_setup_open());assert(!hb_muse_setup_set_local_token("mgst_other",10));
    }
    else if(!strcmp(t,"normal") || !strcmp(t,"saved_phone_disconnect")) {confirm();assert(delays==1);assert(receive("store"));hb_muse_setup_host_step();assert(status().phase==HB_MUSE_SETUP_CONFIRMED && commit_calls==1 && wifi_calls==1);assert(status().wifi_requested && status().account_stored);if(!strcmp(t,"saved_phone_disconnect"))hb_muse_setup_gap_disconnected(7);now_ms+=2000;hb_muse_setup_host_step();assert(status().phase==HB_MUSE_SETUP_SAVED && advertise_calls==1);hb_muse_setup_revoke();hb_muse_setup_host_step();assert(!hb_muse_setup_busy() && release_calls==1);}
    else if(!strcmp(t,"revoke_queue")) {phone();assert(receive("hello"));hb_muse_setup_revoke();assert(!receive("hello"));hb_muse_setup_host_step();assert(!receive_calls && release_calls==1);}
    else if(!strcmp(t,"overflow")) {phone();for(unsigned i=0;i<8;++i)assert(receive("hello"));assert(!receive("hello"));hb_muse_setup_host_step();assert(!receive_calls && status().error==HB_MUSE_SETUP_OVERFLOW);}
    else if(!strcmp(t,"disconnect_reuse")) {phone();assert(receive("hello"));hb_muse_setup_gap_disconnected(7);assert(hb_muse_setup_gap_connected(7,64));hb_muse_setup_gap_subscribed(7,true,64);hb_muse_setup_host_step();assert(!receive_calls);assert(receive("hello"));hb_muse_setup_host_step();assert(receive_calls==1);}
    else if(!strcmp(t,"expired_queue")) {phone();assert(receive("hello"));now_ms+=5000;hb_muse_setup_host_step();assert(!receive_calls && status().error==HB_MUSE_SETUP_TIMEOUT);}
    else if(!strcmp(t,"runtime_fence")) {runtime_status.fenced=false;assert(hb_muse_setup_set_local_token("mgst_fixture",12));assert(hb_muse_setup_open());hb_muse_setup_host_step();assert(!acquire_calls);now_ms+=10000;hb_muse_setup_host_step();assert(status().error==HB_MUSE_SETUP_RUNTIME);}
    else if(!strcmp(t,"runtime_enable")) {phone();runtime_status.enabled=true;hb_muse_setup_host_step();assert(status().error==HB_MUSE_SETUP_RUNTIME && release_calls==1);}
    else if(!strcmp(t,"store_error") || !strcmp(t,"already_paired") || !strcmp(t,"bt_failure")) {
        store_read_ok=strcmp(t,"store_error")!=0;paired=!strcmp(t,"already_paired");bt_ok=strcmp(t,"bt_failure")!=0;
        assert(hb_muse_setup_set_local_token("mgst_fixture",12));assert(hb_muse_setup_open());hb_muse_setup_host_step();assert(status().phase==HB_MUSE_SETUP_FAILED && !advertise_calls);
    }
    else if(!strcmp(t,"wrong_ticket")) {phone();assert(!hb_muse_setup_button(123,1,true));assert(receive("hello"));hb_muse_setup_host_step();assert(!hb_muse_setup_button(124,1,true));}
    else if(!strcmp(t,"unsubscribe")) {phone();assert(receive("hello"));hb_muse_setup_host_step();hb_muse_setup_gap_subscribed(7,false,64);hb_muse_setup_host_step();assert(!hb_muse_setup_busy());}
    else if(!strcmp(t,"revoke_wifi") || !strcmp(t,"expire_wifi") || !strcmp(t,"revoke_commit") || !strcmp(t,"wifi_failure")) {
        confirm();revoke_wifi=!strcmp(t,"revoke_wifi");expire_wifi=!strcmp(t,"expire_wifi");revoke_commit=!strcmp(t,"revoke_commit");wifi_ok=strcmp(t,"wifi_failure")!=0;
        assert(receive("store"));hb_muse_setup_host_step();assert(!commit_calls && !status().account_stored && status().phase==HB_MUSE_SETUP_FAILED);
    }
    else if(!strcmp(t,"bad_fragment")) {phone();uint8_t d[]={0xfe,1,2,'x'};assert(gatt_ops.defer_rx(NULL,7,d,sizeof(d)));hb_muse_setup_host_step();assert(!receive_calls && status().error==HB_MUSE_SETUP_PROTOCOL);}
    else if(!strcmp(t,"clock_back")) {phone();--now_ms;hb_muse_setup_host_step();assert(status().error==HB_MUSE_SETUP_CLOCK && !hb_muse_setup_open());}
    else if(!strcmp(t,"notify_failure")) {phone();notify_ok=false;assert(receive("hello"));hb_muse_setup_host_step();assert(status().error==HB_MUSE_SETUP_PROTOCOL);}
    else if(!strcmp(t,"second_central")) {phone();assert(!hb_muse_setup_gap_connected(8,64));hb_muse_setup_gap_disconnected(8);assert(receive("hello"));hb_muse_setup_host_step();assert(receive_calls==1);}
    else if(!strcmp(t,"stack_reset")) {phone();assert(!hb_muse_setup_stack_reset());hb_muse_setup_revoke();hb_muse_setup_host_step();assert(hb_muse_setup_stack_reset());}
    else if(!strcmp(t,"guarded_token")) {bool allow=false;assert(!hb_muse_setup_set_local_token_guarded("mgst_fixture",12,token_guard,&allow));assert(!status().token_loaded);allow=true;assert(hb_muse_setup_set_local_token_guarded("mgst_fixture",12,token_guard,&allow));assert(status().token_loaded);}
    else if(!strcmp(t,"delayed_subscription")) {
        open_setup();assert(hb_muse_setup_gap_connected(7,64));
        for(unsigned i=0;i<10;++i) {now_ms+=10;hb_muse_setup_host_step();assert(hb_muse_setup_busy());assert(!pair_status.connection);}
        assert(!receive("hello"));assert(!receive_calls && !release_calls);
        hb_muse_setup_gap_subscribed(7,true,64);hb_muse_setup_host_step();assert(pair_status.connection);
        assert(receive("hello"));hb_muse_setup_host_step();assert(receive_calls==1 && status().phase==HB_MUSE_SETUP_CONFIRM);
    }
    else if(!strcmp(t,"notify_pacing_revoke")) {phone();revoke_delay=true;assert(receive("hello"));hb_muse_setup_host_step();assert(delays==1 && notify_calls==1 && !hb_muse_setup_busy());}
    else abort();
    assert(!lock_depth);printf("PASS setup %s\n",t);return 0;
}
