/* Actual P4 app/core/client AND C5 endpoint/binding/PCM. Only driver/control
 * callbacks and the SDK boundary are explicit fakes; no API or audio device. */
#include "hb_muse_app.h"
#include "../transport/hb_muse_endpoint.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hb_muse_app_t app;
static hb_muse_endpoint_t endpoint;
static hb_muse_sdk_snapshot_t sdk;
static uint8_t tx[248],last_pcm[248];
static size_t tx_size,last_pcm_size;
static uint64_t now;
static unsigned enables,connects,revokes,mic_starts,mic_stops,begins,audios,ends;
static unsigned sends,pcm_sends,groups;
static bool tx_valid,mic,fail_mic,fail_control,drop_pcm_reply,drop_all,corrupt_status;
static size_t prefix;
static int16_t received[1024];
static unsigned received_count;
typedef enum {
    TEXT_FAULT_NONE,
    TEXT_FAULT_OFFSET,
    TEXT_FAULT_CAPACITY,
    TEXT_FAULT_OVERRUN,
    TEXT_FAULT_SHORT,
    TEXT_FAULT_EMPTY,
    TEXT_FAULT_COUNT
} text_fault_t;
static text_fault_t text_fault;
static bool id_same(hb_muse_sdk_id_t a) { return a.generation==sdk.id.generation && a.turn==sdk.id.turn; }
hb_muse_sdk_dispatch_t hb_muse_sdk_begin(hb_muse_sdk_id_t id) {
    assert(sdk.session_ready && !sdk.id.turn);++begins;sdk.id=id;sdk.phase=HB_MUSE_SDK_BEGIN_PENDING;return HB_MUSE_SDK_QUEUED;
}
hb_muse_sdk_audio_t hb_muse_sdk_audio(hb_muse_sdk_id_t id,uint32_t first,const int16_t *p,size_t n) {
    assert(id_same(id) && first==sdk.accepted_frames && sdk.phase==HB_MUSE_SDK_LISTENING);
    if(n>prefix) {
        n=prefix;
    }
    assert(n<=1024-received_count);
    memcpy(received+received_count,p,n*sizeof(*p));received_count+=(unsigned)n;
    sdk.accepted_frames+=(uint32_t)n;++audios;return (hb_muse_sdk_audio_t){n,false};
}
hb_muse_sdk_dispatch_t hb_muse_sdk_end(hb_muse_sdk_id_t id,uint32_t count) {
    assert(id_same(id) && count==sdk.accepted_frames);++ends;sdk.phase=HB_MUSE_SDK_END_PENDING;return HB_MUSE_SDK_QUEUED;
}
hb_muse_sdk_dispatch_t hb_muse_sdk_cancel(hb_muse_sdk_id_t id) {
    assert(id_same(id));sdk.phase=HB_MUSE_SDK_CANCEL_PENDING;return HB_MUSE_SDK_QUEUED;
}
bool hb_muse_sdk_snapshot(hb_muse_sdk_snapshot_t *out) { *out=sdk;return true; }
bool hb_muse_sdk_retire(hb_muse_sdk_id_t id) {
    assert(id_same(id) && (sdk.phase==HB_MUSE_SDK_DONE || sdk.phase==HB_MUSE_SDK_FENCED));
    memset(&sdk,0,sizeof(sdk));sdk.worker_started=sdk.session_ready=true;return true;
}
static bool control(void *u,hb_muse_app_control_t op) {
    assert(u==&endpoint);
    if(op==HB_MUSE_APP_ENABLE)++enables;
    if(op==HB_MUSE_APP_CONNECT) { ++connects;hb_muse_endpoint_access(&endpoint,true,true,true); }
    if(op==HB_MUSE_APP_REVOKE) { ++revokes;hb_muse_endpoint_access(&endpoint,false,true,true); }
    return !fail_control;
}
static bool send_frame(void *u,const uint8_t *p,size_t n) {
    assert(u==&endpoint && !tx_valid && n<=248);memcpy(tx,p,n);tx_size=n;tx_valid=true;++sends;return true;
}
static bool start_mic(void *u,uint32_t rate) {
    assert(u==&endpoint && rate==16000 && !mic);++mic_starts;if(fail_mic)return false;mic=true;return true;
}
static void stop_mic(void *u) { assert(u==&endpoint);++mic_stops;mic=false; }
static hb_muse_app_view_t view(void) { hb_muse_app_view_t v;assert(hb_muse_app_view(&app,&v));return v; }
static void cycle(unsigned elapsed) {
    now+=elapsed;hb_muse_endpoint_pump(&endpoint,now);hb_muse_app_pump(&app,now);
    if(!tx_valid)return;
    uint8_t response[248];size_t n=0;hb_muse_wire_frame_t request,reply;
    assert(hb_muse_wire_decode(tx,tx_size,&request));tx_valid=false;
    assert(hb_muse_endpoint_process(&endpoint,tx,tx_size,now,response,sizeof(response),&n));
    assert(hb_muse_wire_decode(response,n,&reply));
    if(request.opcode==HB_MUSE_WIRE_PCM) {
        ++pcm_sends;
        if(drop_pcm_reply) { drop_pcm_reply=false;memcpy(last_pcm,tx,tx_size);last_pcm_size=tx_size;return; }
        if(last_pcm_size) { assert(last_pcm_size==tx_size && !memcmp(last_pcm,tx,tx_size));last_pcm_size=0; }
    }
    if(drop_all)return;
    if(corrupt_status && request.opcode==HB_MUSE_WIRE_STATUS) {
        corrupt_status=false;reply.payload[30]=1;assert(hb_muse_wire_encode(&reply,response,sizeof(response),&n));
    }
    if(request.opcode==HB_MUSE_WIRE_TEXT && text_fault!=TEXT_FAULT_NONE) {
        if(text_fault==TEXT_FAULT_OFFSET || text_fault==TEXT_FAULT_OVERRUN) {
            assert(app.text_total>0);
            app.text_offset=(uint16_t)(text_fault==TEXT_FAULT_OFFSET ? app.text_total+1 : app.text_total-1);
            reply.payload[10]=(uint8_t)app.text_offset;
            reply.payload[11]=(uint8_t)(app.text_offset>>8);
        } else if(text_fault==TEXT_FAULT_CAPACITY) {
            app.text_total=(uint16_t)sizeof(app.text);
            reply.payload[8]=(uint8_t)app.text_total;
            reply.payload[9]=(uint8_t)(app.text_total>>8);
        } else {
            reply.payload_len=(uint16_t)(text_fault==TEXT_FAULT_SHORT ? 11 : 12);
        }
        text_fault=TEXT_FAULT_NONE;
        assert(hb_muse_wire_encode(&reply,response,sizeof(response),&n));
    }
    hb_muse_app_receive(&app,response,n,now);
}
static void setup(void) {
    memset(&sdk,0,sizeof(sdk));sdk.worker_started=true;
    now=0;enables=connects=revokes=mic_starts=mic_stops=begins=audios=ends=0;
    sends=pcm_sends=received_count=0;tx_size=last_pcm_size=0;
    tx_valid=mic=fail_mic=fail_control=drop_pcm_reply=drop_all=corrupt_status=false;prefix=106;
    text_fault=TEXT_FAULT_NONE;
    assert(hb_muse_endpoint_init(&endpoint,42));hb_muse_endpoint_access(&endpoint,false,true,true);
    const hb_muse_app_ops_t ops={control,send_frame,start_mic,stop_mic};
    assert(hb_muse_app_init(&app,&ops,&endpoint));assert(view().phase==HB_MUSE_APP_DISABLED && !sends && !mic_starts);
}
static void connect_ready(void) {
    setup();assert(hb_muse_app_confirm(&app,now));assert(enables==1 && !connects && !mic_starts);
    assert(hb_muse_app_connect(&app,0x1020304050607080ULL,now));
    for(unsigned i=0;i<6;++i)cycle(1);
    assert(!view().sdk_ready && view().phase==HB_MUSE_APP_CONNECTING && !mic_starts);
    sdk.session_ready=true;cycle(101);cycle(1);
    assert(view().sdk_ready && view().phase==HB_MUSE_APP_READY && !mic_starts);
}
static void listening(void) {
    connect_ready();assert(hb_muse_app_physical_ptt(&app,true,now)==HB_MUSE_NOT_READY);
    (void)hb_muse_app_physical_ptt(&app,false,now);
    assert(hb_muse_app_physical_ptt(&app,true,now)==HB_MUSE_OK);
    cycle(1);cycle(1);assert(begins==1 && !mic_starts && !hb_muse_app_pcm_capacity(&app));
    sdk.begin_ack=true;sdk.phase=HB_MUSE_SDK_LISTENING;cycle(101);cycle(1);
    assert(mic && view().phase==HB_MUSE_APP_LISTENING && hb_muse_app_pcm_capacity(&app)==320);
}
static void test_no_auto(void) {
    setup();for(unsigned i=0;i<10;++i)cycle(1);assert(!enables && !connects && !sends && !mic_starts);
    connect_ready();assert(hb_muse_app_physical_ptt(&app,true,now)==HB_MUSE_NOT_READY);
    for(unsigned i=0;i<5;++i) {
        cycle(100);
    }
    assert(!begins && !mic_starts);
    hb_muse_app_close(&app);assert(revokes==1 && view().phase==HB_MUSE_APP_DISABLED);
    ++groups;
}
static void test_begin_ack(void) {
    connect_ready();(void)hb_muse_app_physical_ptt(&app,false,now);
    assert(hb_muse_app_physical_ptt(&app,true,now)==HB_MUSE_OK);cycle(1);cycle(1);
    assert(begins==1 && !mic && !view().mic_active);
    assert(hb_muse_app_physical_ptt(&app,false,now)==HB_MUSE_OK);
    assert(!mic_starts && revokes==1 && !app.connecting);cycle(100);assert(!mic_starts && !ends);
    ++groups;
}
static void test_audio_release(void) {
    listening();int16_t p[320];for(unsigned i=0;i<320;++i)p[i]=(int16_t)((int)i-160);
    assert(hb_muse_app_pcm(&app,p,320)==HB_MUSE_OK && !hb_muse_app_pcm_capacity(&app));
    assert(hb_muse_app_pcm(&app,p,1)==HB_MUSE_BACKPRESSURE);
    assert(hb_muse_app_physical_ptt(&app,false,now)==HB_MUSE_OK && !mic);
    assert(hb_muse_app_pcm(&app,p,1)==HB_MUSE_NOT_READY);
    prefix=37;for(unsigned i=0;i<30 && !ends;++i)cycle(1);
    assert(ends==1 && pcm_sends==4 && received_count==320 && !memcmp(p,received,sizeof(p)));
    assert(view().acknowledged_frames==320 && view().phase==HB_MUSE_APP_WAITING);
    ++groups;
}
static void test_replay(void) {
    listening();int16_t p[10]={1,2,3,4,5};assert(hb_muse_app_pcm(&app,p,10)==HB_MUSE_OK);
    drop_pcm_reply=true;cycle(1);cycle(1);assert(received_count==10 && pcm_sends==1);
    cycle(250);assert(pcm_sends==2 && received_count==10 && view().acknowledged_frames==10);
    cycle(1);assert(received_count==10);++groups;
}
static void test_cancel(void) {
    listening();int16_t p[10]={1};assert(hb_muse_app_pcm(&app,p,10)==HB_MUSE_OK);
    hb_muse_app_cancel(&app);assert(!mic && !hb_muse_app_pcm_capacity(&app) && revokes==1);
    for(unsigned i=0;i<320;++i)assert(app.pcm[i]==0);
    for(unsigned i=0;i<10;++i) {
        cycle(100);
    }
    assert(!audios && !ends && !pcm_sends && !app.connecting);
    ++groups;
}
static void test_timeout(void) {
    listening();drop_all=true;int16_t p[10]={1};assert(hb_muse_app_pcm(&app,p,10)==HB_MUSE_OK);
    cycle(1);cycle(250);cycle(250);cycle(250);
    assert(view().phase==HB_MUSE_APP_ERROR && !mic && revokes==1 && pcm_sends==3);
    unsigned count=sends;for(unsigned i=0;i<10;++i)cycle(100);assert(sends==count && connects==1);
    ++groups;
}
static void waiting(void) {
    listening();int16_t p[1]={42};assert(hb_muse_app_pcm(&app,p,1)==HB_MUSE_OK);
    (void)hb_muse_app_physical_ptt(&app,false,now);
    for(unsigned i=0;i<10 && !ends;++i) {
        cycle(1);
    }
    assert(ends==1);
    sdk.end_ack=sdk.note_ack=true;sdk.phase=HB_MUSE_SDK_WAIT_REPLY;cycle(101);
}
static void test_text(void) {
    waiting();char expected[610];memset(expected,'A',sizeof(expected));
    expected[203]=(char)0xe2;expected[204]=(char)0x82;expected[205]=(char)0xac;
    expected[400]='\n';expected[609]=0;
    memcpy(sdk.text,expected,sizeof(expected));sdk.text_revision=1;sdk.message_index=2;sdk.phase=HB_MUSE_SDK_DONE;
    for(unsigned i=0;i<30 && app.turn_active;++i)cycle(i ? 1 : 101);
    expected[400]=' ';assert(view().text_complete && view().text_bytes==609 && !strcmp(view().text,expected));
    assert(view().message_index==2 && !app.turn_active && !mic && !revokes);
    /* Completed reply must release the turn for a fresh physical press. */
    assert(hb_muse_app_physical_ptt(&app,true,now)==HB_MUSE_OK);
    cycle(1);cycle(1);assert(begins==2 && !mic);
    sdk.begin_ack=true;sdk.phase=HB_MUSE_SDK_LISTENING;
    cycle(101);cycle(1);assert(mic && app.turn_active && !view().text_complete);
    hb_muse_app_cancel(&app);assert(!mic);
    ++groups;
}
static void test_revision(void) {
    waiting();memset(sdk.text,'X',500);sdk.text[500]=0;sdk.text_revision=1;sdk.message_index=1;
    cycle(101);cycle(1);assert(app.text_offset==204 && !view().text_complete);
    strcpy(sdk.text,"New actual fixture reply");sdk.text_revision=2;cycle(1);
    assert(!view().text_complete && !view().text_bytes);
    for(unsigned i=0;i<8 && !view().text_complete;++i)cycle(1);
    assert(view().text_complete && !strcmp(view().text,"New actual fixture reply"));++groups;
}
static void test_invalid_text(void) {
    waiting();sdk.text[0]=(char)0xc0;sdk.text[1]=(char)0xaf;sdk.text[2]=0;sdk.text_revision=1;
    cycle(101);cycle(1);assert(view().phase==HB_MUSE_APP_ERROR && !view().text_complete && revokes==1);++groups;
}
static void test_text_bounds(void) {
    for(text_fault_t fault=TEXT_FAULT_OFFSET;fault<TEXT_FAULT_COUNT;++fault) {
        waiting();
        strcpy(sdk.text,"Bounded reply");
        sdk.text_revision=1;
        text_fault=fault;
        cycle(101);
        cycle(1);
        assert(text_fault==TEXT_FAULT_NONE);
        assert(view().phase==HB_MUSE_APP_ERROR && view().error==HB_MUSE_APP_BAD_RESPONSE);
        assert(!view().text_complete && revokes==1 && !mic);
        ++groups;
    }
}
static void test_bad_status(void) {
    listening();corrupt_status=true;cycle(101);assert(view().phase==HB_MUSE_APP_ERROR && !mic && revokes==1);++groups;
}
static void test_mic_failure(void) {
    connect_ready();(void)hb_muse_app_physical_ptt(&app,false,now);(void)hb_muse_app_physical_ptt(&app,true,now);
    cycle(1);cycle(1);sdk.begin_ack=true;sdk.phase=HB_MUSE_SDK_LISTENING;fail_mic=true;cycle(101);
    assert(view().phase==HB_MUSE_APP_ERROR && view().error==HB_MUSE_APP_MIC_FAILED && mic_stops==1 && !mic);++groups;
}
static void test_control_failure(void) {
    setup();fail_control=true;assert(!hb_muse_app_confirm(&app,now));assert(enables==1 && revokes==1 && !sends && !mic_starts);++groups;
}
static void test_clock(void) {
    listening();hb_muse_app_pump(&app,0);assert(view().phase==HB_MUSE_APP_ERROR && !mic && revokes==1);++groups;
}
static void test_stale_response(void) {
    connect_ready();uint8_t reply[248];size_t n=0;
    hb_muse_wire_frame_t f={.boot=42,.epoch=999,.sequence=999,.opcode=HB_MUSE_WIRE_STATUS,
       .flags=HB_MUSE_WIRE_RESPONSE,.payload_len=32};
    assert(hb_muse_wire_encode(&f,reply,sizeof(reply),&n));
    hb_muse_app_receive(&app,reply,n,now);assert(view().phase==HB_MUSE_APP_READY && !mic_starts);
    reply[0]^=1;hb_muse_app_receive(&app,reply,n,now);assert(view().phase==HB_MUSE_APP_READY);++groups;
}
static void test_recording_limit(void) {
    listening();
    /* Boundary-state fixture: the core has already accepted 239999 samples.
     * No synthetic PCM is sent; prove the final accepted sample stops I2S. */
    app.core.accepted_frames=HB_MUSE_APP_MAX_FRAMES-1;
    int16_t last=7;assert(hb_muse_app_pcm(&app,&last,1)==HB_MUSE_OK);
    assert(!mic && !view().mic_active && !hb_muse_app_pcm_capacity(&app));
    assert(app.end_needed && app.end_total==HB_MUSE_APP_MAX_FRAMES && !app.armed);
    assert(hb_muse_app_physical_ptt(&app,true,now)==HB_MUSE_NOT_READY);
    hb_muse_app_close(&app);++groups;
}
int main(void) {
    test_no_auto();test_begin_ack();test_audio_release();test_replay();test_cancel();test_timeout();
    test_text();test_revision();test_invalid_text();test_text_bounds();test_bad_status();test_mic_failure();test_control_failure();test_clock();test_stale_response();test_recording_limit();
    printf("P4 app: %u strict/UBSan groups; static app=%zu bytes; explicit SDK/driver fixtures only\n",groups,sizeof(app));return 0;
}
