/* Real wire/server/endpoint/binding/PCM code. Explicit fake SDK boundary.
 * No hardware, credentials, cloud, real RTOS or recorded microphone data. */
#include "hb_muse_endpoint.h"
#include "hb_muse_wire_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hb_muse_sdk_snapshot_t sdk;
static unsigned begins, audio_calls, ends, cancels, retires;
static bool block_audio, reject_snapshot, cancel_blocked, reject_begin;
static size_t prefix = 106;
static int16_t last_sample;
static hb_muse_endpoint_t endpoint;
static uint32_t sequence;
static uint64_t clock_ms;
static uint8_t request_bytes[248], response_bytes[248];
static size_t request_size, response_size;
static unsigned groups;
static bool same(hb_muse_sdk_id_t id) { return id.generation == sdk.id.generation && id.turn == sdk.id.turn; }
hb_muse_sdk_dispatch_t hb_muse_sdk_begin(hb_muse_sdk_id_t id) {
    assert(!sdk.id.turn); ++begins;
    if (reject_begin) return HB_MUSE_SDK_REJECTED;
    sdk.id = id; sdk.phase = HB_MUSE_SDK_BEGIN_PENDING;
    return HB_MUSE_SDK_QUEUED;
}
hb_muse_sdk_audio_t hb_muse_sdk_audio(hb_muse_sdk_id_t id, uint32_t first, const int16_t *pcm, size_t n) {
    assert(same(id)); assert(sdk.phase == HB_MUSE_SDK_LISTENING);
    assert(first == sdk.accepted_frames); ++audio_calls;
    if (block_audio) return (hb_muse_sdk_audio_t){0, false};
    if (n > prefix) n = prefix;
    last_sample = pcm[n - 1]; sdk.accepted_frames += (uint32_t)n;
    return (hb_muse_sdk_audio_t){n, false};
}
hb_muse_sdk_dispatch_t hb_muse_sdk_end(hb_muse_sdk_id_t id, uint32_t total) {
    assert(same(id)); assert(total == sdk.accepted_frames); ++ends;
    sdk.phase = HB_MUSE_SDK_END_PENDING; return HB_MUSE_SDK_QUEUED;
}
hb_muse_sdk_dispatch_t hb_muse_sdk_cancel(hb_muse_sdk_id_t id) {
    assert(same(id)); ++cancels; sdk.phase = HB_MUSE_SDK_CANCEL_PENDING;
    return cancel_blocked ? HB_MUSE_SDK_BLOCKED : HB_MUSE_SDK_QUEUED;
}
bool hb_muse_sdk_snapshot(hb_muse_sdk_snapshot_t *out) {
    if (reject_snapshot) return false;
    *out = sdk; return true;
}
bool hb_muse_sdk_retire(hb_muse_sdk_id_t id) {
    assert(same(id)); assert(sdk.phase == HB_MUSE_SDK_DONE || sdk.phase == HB_MUSE_SDK_FENCED);
    ++retires; memset(&sdk, 0, sizeof(sdk)); sdk.worker_started = sdk.session_ready = true; return true;
}
static void put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put16(uint8_t *p, unsigned v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void pump(void) { hb_muse_endpoint_pump(&endpoint, ++clock_ms); }
static hb_muse_wire_frame_t raw(hb_muse_wire_frame_t r) {
    hb_muse_wire_frame_t out;
    if (r.opcode==HB_MUSE_WIRE_OPEN && !r.payload_len) {
        r.payload_len=8;r.payload[0]=0x4f;r.payload[4]=(uint8_t)r.epoch;
    }
    assert(hb_muse_wire_encode(&r, request_bytes, sizeof(request_bytes), &request_size));
    assert(hb_muse_endpoint_process(&endpoint, request_bytes, request_size, ++clock_ms,
        response_bytes, sizeof(response_bytes), &response_size));
    assert(hb_muse_wire_decode(response_bytes, response_size, &out));
    assert(out.flags == 1 && out.opcode == r.opcode && out.sequence == r.sequence);
    return out;
}
static hb_muse_wire_frame_t command(unsigned op, uint32_t gen, uint32_t turn, const void *p, size_t n) {
    hb_muse_wire_frame_t r = {.boot=42,.epoch=endpoint.epoch,.sequence=++sequence,
        .generation=gen,.turn=turn,.opcode=(uint8_t)op,.payload_len=(uint16_t)n};
    if(n) memcpy(r.payload,p,n);
    return raw(r);
}
static hb_muse_wire_frame_t active(unsigned op, const void *p, size_t n) {
    return command(op,7,9,p,n);
}
static void setup(void) {
    memset(&sdk,0,sizeof(sdk)); sdk.worker_started=sdk.session_ready=true;
    begins=audio_calls=ends=cancels=retires=0; block_audio=reject_snapshot=cancel_blocked=reject_begin=false;
    prefix=106;clock_ms=0;sequence=1;
    assert(hb_muse_endpoint_init(&endpoint,42));
    hb_muse_endpoint_access(&endpoint,true,true,true);pump();
    hb_muse_wire_frame_t out=raw((hb_muse_wire_frame_t){.boot=42,.epoch=1,.sequence=1,.opcode=HB_MUSE_WIRE_OPEN});
    assert(out.status==HB_MUSE_WIRE_OK && endpoint.epoch==1 && begins==0);
}
static void begin(void) {
    unsigned previous_begins = begins;
    assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==HB_MUSE_WIRE_OK);
    assert(begins==previous_begins);pump();assert(begins==previous_begins+1 && !endpoint.begin_ack);
    sdk.begin_ack=true;sdk.phase=HB_MUSE_SDK_LISTENING;pump();
    assert(endpoint.phase==HB_MUSE_ENDPOINT_LISTENING);
}
static hb_muse_wire_frame_t pcm(uint32_t first,size_t n) {
    uint8_t p[216];assert(n<=106);put32(p,first);
    for(size_t i=0;i<n;++i) put16(p+4+2*i,i+1==n ? 0x8000 : (unsigned)i);
    return active(HB_MUSE_WIRE_PCM,p,4+2*n);
}
static void end(void) {
    uint8_t p[4];put32(p,endpoint.accepted);
    assert(active(HB_MUSE_WIRE_END,p,4).status==HB_MUSE_WIRE_OK);
    pump();assert(ends==1);sdk.end_ack=sdk.note_ack=true;sdk.phase=HB_MUSE_SDK_WAIT_REPLY;pump();
    assert(endpoint.phase==HB_MUSE_ENDPOINT_WAITING);
}
static void done(void) {
    sdk.phase=HB_MUSE_SDK_DONE;sdk.text_revision=1;memcpy(sdk.text,"Real SDK fixture reply",23);
    pump();assert(endpoint.phase==HB_MUSE_ENDPOINT_DONE && retires==1);
}
static void test_gates(void) {
    setup();hb_muse_endpoint_access(&endpoint,false,true,true);
    assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==HB_MUSE_WIRE_BLOCKED);
    hb_muse_endpoint_access(&endpoint,true,false,true);
    assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==HB_MUSE_WIRE_BLOCKED);
    hb_muse_endpoint_access(&endpoint,true,true,true);sdk.session_ready=false;pump();
    assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==HB_MUSE_WIRE_BLOCKED && begins==0);
    ++groups;
}
static void test_ack_and_audio(void) {
    setup();assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==0);
    assert(pcm(0,1).status==HB_MUSE_WIRE_REJECTED && audio_calls==0);
    pump();assert(endpoint.phase==HB_MUSE_ENDPOINT_STARTING);
    sdk.begin_ack=true;sdk.phase=HB_MUSE_SDK_LISTENING;pump();
    assert(pcm(0,106).status==0 && endpoint.accepted==106 && sdk.accepted_frames==0);
    uint8_t replay[248];size_t len=request_size;memcpy(replay,request_bytes,len);
    assert(hb_muse_endpoint_process(&endpoint,replay,len,++clock_ms,response_bytes,248,&response_size));
    assert(endpoint.accepted==106);prefix=3;pump();assert(sdk.accepted_frames==3);
    prefix=106;pump();assert(sdk.accepted_frames==106 && last_sample==-32768);
    assert(pcm(0,1).status==HB_MUSE_WIRE_REJECTED && endpoint.accepted==106);
    assert(command(HB_MUSE_WIRE_CANCEL,7,8,NULL,0).status==HB_MUSE_WIRE_REJECTED);
    ++groups;
}
static void test_backpressure(void) {
    setup();begin();block_audio=true;
    for(unsigned i=0;i<12;++i) assert(pcm(i*106,106).status==0);
    assert(endpoint.binding.adapter.queued==1272);
    assert(pcm(1272,106).status==HB_MUSE_WIRE_BLOCKED && endpoint.accepted==1272);
    assert(pcm(1272,8).status==0);
    assert(pcm(1280,1).status==HB_MUSE_WIRE_BLOCKED && endpoint.accepted==1280);
    assert(active(HB_MUSE_WIRE_CANCEL,NULL,0).status==0 && endpoint.binding.adapter.queued==0);
    assert(pcm(1280,1).status==HB_MUSE_WIRE_REJECTED);++groups;
}
static void test_text_and_retire(void) {
    setup();begin();assert(pcm(0,10).status==0);pump();end();
    sdk.text_revision=1;sdk.message_index=3;memset(sdk.text,'X',sizeof(sdk.text)-1);pump();
    hb_muse_wire_frame_t status=active(HB_MUSE_WIRE_STATUS,NULL,0);
    assert(status.payload_len==32 && get32(status.payload+12)==1 && get32(status.payload+4)==10);
    uint8_t q[6]={1,0,0,0,0,0};
    hb_muse_wire_frame_t t=active(HB_MUSE_WIRE_TEXT,q,6);
    assert(t.status==0 && t.payload_len==216 && t.payload[12]=='X' && get32(t.payload+4)==3);
    sdk.text_revision=2;sdk.text[0]='Y';pump();
    q[4]=204;assert(active(HB_MUSE_WIRE_TEXT,q,6).status==HB_MUSE_WIRE_BLOCKED);
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==HB_MUSE_WIRE_BUSY);
    sdk.phase=HB_MUSE_SDK_DONE;sdk.text_revision=3;memset(sdk.text,0,sizeof(sdk.text));memcpy(sdk.text,"Answer",7);
    pump();pump();pump();assert(endpoint.phase==HB_MUSE_ENDPOINT_DONE && endpoint.text_bytes==6);
    put32(q,3);q[4]=0;t=active(HB_MUSE_WIRE_TEXT,q,6);assert(t.payload_len==18 && !memcmp(t.payload+12,"Answer",6));
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==0 && endpoint.text_bytes==0);
    assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==HB_MUSE_WIRE_REJECTED);
    assert(command(HB_MUSE_WIRE_BEGIN,7,10,NULL,0).status==0);pump();
    assert(sdk.id.generation==1 && sdk.id.turn==2 && begins==2);++groups;
}
static void test_cancel_fence(void) {
    setup();begin();assert(pcm(0,10).status==0);
    cancel_blocked=true;assert(active(HB_MUSE_WIRE_CANCEL,NULL,0).status==0);pump();
    assert(endpoint.binding.adapter.queued==0 && cancels==1);
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==HB_MUSE_WIRE_BUSY);
    assert(command(HB_MUSE_WIRE_BEGIN,8,1,NULL,0).status==HB_MUSE_WIRE_BUSY);
    cancel_blocked=false;sdk.phase=HB_MUSE_SDK_FENCED;pump();
    assert(endpoint.phase==HB_MUSE_ENDPOINT_CANCELLED && retires==1);
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==0);++groups;
}
static void test_lease_reopen(void) {
    setup();begin();assert(pcm(0,10).status==0);
    clock_ms+=2001;hb_muse_endpoint_pump(&endpoint,clock_ms);
    assert(!endpoint.server.active && endpoint.binding.adapter.queued==0 && cancels==1);
    hb_muse_wire_frame_t open={.boot=42,.epoch=2,.sequence=1,.opcode=HB_MUSE_WIRE_OPEN};
    assert(raw(open).status==HB_MUSE_WIRE_BUSY);
    sdk.phase=HB_MUSE_SDK_FENCED;pump();assert(raw(open).status==0);
    sequence=1;assert(!endpoint.active && endpoint.epoch==2);
    begin();assert(sdk.id.generation==2 && sdk.id.turn==1);++groups;
}
static void test_access_revoke_and_empty_cancel(void) {
    setup();assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==0);
    hb_muse_endpoint_access(&endpoint,false,true,true);
    assert(endpoint.binding.adapter.phase==HB_MUSE_ADAPTER_IDLE && begins==0);pump();
    assert(endpoint.phase==HB_MUSE_ENDPOINT_CANCELLED && cancels==0);
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==0);
    setup();begin();assert(pcm(0,3).status==0);
    hb_muse_endpoint_access(&endpoint,true,true,false);
    assert(endpoint.binding.adapter.queued==0);pump();assert(cancels==1);++groups;
}
static void test_failure_and_clock(void) {
    setup();begin();reject_snapshot=true;pump();
    assert(endpoint.phase==HB_MUSE_ENDPOINT_FAILED && endpoint.binding.adapter.queued==0);
    reject_snapshot=false;sdk.phase=HB_MUSE_SDK_FENCED;pump();
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==0);
    setup();begin();hb_muse_endpoint_pump(&endpoint,clock_ms-1);
    assert(endpoint.server.clock_fault && endpoint.binding.clock_fault && cancels==1);++groups;
}
static void test_malformed_and_buffer(void) {
    setup();begin();assert(pcm(0,1).status==0);
    uint32_t count=endpoint.accepted;request_bytes[request_size-1]^=1;
    assert(!hb_muse_endpoint_process(&endpoint,request_bytes,request_size,++clock_ms,response_bytes,248,&response_size));
    assert(endpoint.accepted==count);
    hb_muse_wire_frame_t r={.boot=42,.epoch=1,.sequence=sequence+1,.generation=7,.turn=9,.opcode=HB_MUSE_WIRE_PCM,.payload_len=5};
    assert(raw(r).status==HB_MUSE_WIRE_REJECTED);++sequence;
    r.sequence=sequence+1;r.payload_len=6;put32(r.payload,1);
    assert(hb_muse_wire_encode(&r,request_bytes,248,&request_size));
    assert(!hb_muse_endpoint_process(&endpoint,request_bytes,request_size,++clock_ms,response_bytes,247,&response_size));
    assert(endpoint.accepted==1 && endpoint.server.last_sequence==sequence);++groups;
}
static void test_done_no_restart(void) {
    setup();begin();assert(pcm(0,1).status==0);pump();end();done();
    assert(active(HB_MUSE_WIRE_CANCEL,NULL,0).status==0);pump();
    assert(endpoint.phase==HB_MUSE_ENDPOINT_DONE && cancels==0);
    assert(command(HB_MUSE_WIRE_BEGIN,8,1,NULL,0).status==HB_MUSE_WIRE_BUSY);
    clock_ms+=2001;pump();assert(!endpoint.server.active && endpoint.phase==HB_MUSE_ENDPOINT_DONE && begins==1);
    ++groups;
}
static void test_end_drains_accepted_prefixes(void) {
    setup();begin();prefix=3;assert(pcm(0,10).status==0);
    uint8_t total[4];put32(total,10);
    assert(active(HB_MUSE_WIRE_END,total,4).status==0);
    for(unsigned i=0;i<4;++i) { pump();assert(ends==0); }
    assert(sdk.accepted_frames==10 && endpoint.binding.adapter.queued==0);
    pump();assert(ends==1 && endpoint.phase==HB_MUSE_ENDPOINT_FINISHING);
    assert(pcm(10,1).status==HB_MUSE_WIRE_REJECTED);++groups;
}
static void test_foreign_sdk_snapshot(void) {
    setup();begin();assert(pcm(0,3).status==0);pump();end();
    hb_muse_sdk_id_t id=sdk.id;sdk.id.turn+=1;
    sdk.phase=HB_MUSE_SDK_DONE;sdk.text_revision=6;memcpy(sdk.text,"Foreign",8);pump();
    assert(endpoint.text_bytes==0 && endpoint.phase==HB_MUSE_ENDPOINT_WAITING && retires==0);
    sdk.phase=HB_MUSE_SDK_FENCED;pump();assert(retires==0 && endpoint.binding.owns_sdk);
    sdk.id=id;sdk.phase=HB_MUSE_SDK_WAIT_REPLY;memset(sdk.text,0,sizeof(sdk.text));sdk.text_revision=0;
    assert(active(HB_MUSE_WIRE_CANCEL,NULL,0).status==0);pump();
    sdk.phase=HB_MUSE_SDK_FENCED;pump();assert(retires==1);++groups;
}
static void test_failed_text_is_not_done(void) {
    setup();begin();assert(pcm(0,3).status==0);pump();end();
    sdk.text_revision=4;memcpy(sdk.text,"Partial",8);pump();
    sdk.phase=HB_MUSE_SDK_FAILED;pump();assert(endpoint.phase==HB_MUSE_ENDPOINT_FAILED);
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==HB_MUSE_WIRE_BUSY);
    sdk.phase=HB_MUSE_SDK_FENCED;pump();pump();
    hb_muse_wire_frame_t status=active(HB_MUSE_WIRE_STATUS,NULL,0);
    assert(status.payload[0]==HB_MUSE_ENDPOINT_FAILED && (status.payload[1]&128));
    uint8_t q[6]={4,0,0,0,0,0};hb_muse_wire_frame_t text=active(HB_MUSE_WIRE_TEXT,q,6);
    assert(text.payload_len==19 && !memcmp(text.payload+12,"Partial",7));
    assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==0);++groups;
}
static void test_sdk_rejects_local_begin(void) {
    setup();reject_begin=true;
    assert(active(HB_MUSE_WIRE_BEGIN,NULL,0).status==0 && begins==0);pump();
    assert(endpoint.phase==HB_MUSE_ENDPOINT_FAILED && !endpoint.begin_ack);
    pump();assert(active(HB_MUSE_WIRE_RETIRE,NULL,0).status==0 && cancels==0);++groups;
}
static void test_sample_limit(void) {
    setup();begin();uint32_t sent=0;
    while(sent<HB_MUSE_SDK_MAX_FRAMES) {
        size_t count=HB_MUSE_SDK_MAX_FRAMES-sent;if(count>106) count=106;
        assert(pcm(sent,count).status==0);
        do { pump(); } while(endpoint.binding.adapter.queued);
        sent+=(uint32_t)count;
    }
    assert(endpoint.accepted==HB_MUSE_SDK_MAX_FRAMES);
    assert(pcm(sent,1).status==HB_MUSE_WIRE_REJECTED);
    assert(endpoint.phase==HB_MUSE_ENDPOINT_FAILED && endpoint.binding.adapter.queued==0);++groups;
}
static hb_muse_wire_frame_t exchange(hb_muse_wire_client_t *client,bool drop_reply) {
    uint8_t request[248],response[248];size_t n,m;hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_next_request(client,++clock_ms,request,sizeof(request),&n));
    assert(hb_muse_endpoint_process(&endpoint,request,n,++clock_ms,response,sizeof(response),&m));
    if(drop_reply) assert(hb_muse_wire_decode(response,m,&out));
    else assert(hb_muse_wire_client_accept_response(client,response,m,++clock_ms,&out)==HB_MUSE_CLIENT_RESPONSE);
    return out;
}
static void connect_client(hb_muse_wire_client_t *client) {
    setup();assert(command(HB_MUSE_WIRE_CLOSE,0,0,NULL,0).status==0);
    assert(hb_muse_wire_client_init(client,0));
    assert(hb_muse_wire_client_prepare_hello(client,UINT64_C(0x83b72fe091340ca1),++clock_ms));
    assert(exchange(client,false).status==0 && client->state==HB_MUSE_CLIENT_CAN_OPEN);
    assert(hb_muse_wire_client_prepare_open(client,++clock_ms));
    assert(exchange(client,false).status==0 && client->state==HB_MUSE_CLIENT_ACTIVE && endpoint.epoch==2);
}
static void client_command(hb_muse_wire_client_t *client,unsigned op,const uint8_t *p,size_t n) {
    assert(hb_muse_wire_client_prepare_command(client,(uint8_t)op,7,9,p,n,++clock_ms));
}
static void test_full_transport_binding_chain(void) {
    hb_muse_wire_client_t client;connect_client(&client);
    client_command(&client,HB_MUSE_WIRE_BEGIN,NULL,0);assert(exchange(&client,false).status==0);pump();
    sdk.begin_ack=true;sdk.phase=HB_MUSE_SDK_LISTENING;pump();
    client_command(&client,HB_MUSE_WIRE_STATUS,NULL,0);
    assert(exchange(&client,false).payload[0]==HB_MUSE_ENDPOINT_LISTENING);
    uint8_t pcm_bytes[6]={0,0,0,0,0,0x80};client_command(&client,HB_MUSE_WIRE_PCM,pcm_bytes,6);
    assert(exchange(&client,true).status==0);pump();assert(sdk.accepted_frames==1 && last_sample==-32768);
    clock_ms+=250;assert(exchange(&client,false).status==0);pump();
    assert(sdk.accepted_frames==1 && endpoint.accepted==1 && audio_calls==1);
    uint8_t total[4]={1,0,0,0};client_command(&client,HB_MUSE_WIRE_END,total,4);
    assert(exchange(&client,false).status==0);pump();assert(ends==1);
    sdk.end_ack=sdk.note_ack=true;sdk.phase=HB_MUSE_SDK_WAIT_REPLY;pump();done();
    client_command(&client,HB_MUSE_WIRE_STATUS,NULL,0);
    assert(exchange(&client,false).payload[0]==HB_MUSE_ENDPOINT_DONE);
    uint8_t text_request[6]={1,0,0,0,0,0};client_command(&client,HB_MUSE_WIRE_TEXT,text_request,6);
    hb_muse_wire_frame_t text=exchange(&client,false);
    assert(text.payload_len==34 && !memcmp(text.payload+12,"Real SDK fixture reply",22));
    client_command(&client,HB_MUSE_WIRE_RETIRE,NULL,0);assert(exchange(&client,false).status==0);
    assert(hb_muse_wire_client_prepare_command(&client,HB_MUSE_WIRE_CLOSE,0,0,NULL,0,++clock_ms));
    assert(exchange(&client,false).status==0 && client.state==HB_MUSE_CLIENT_NEED_HELLO);
    assert(begins==1 && ends==1 && cancels==0 && retires==1);++groups;
}
static void test_client_abandon_remote_fence(void) {
    hb_muse_wire_client_t client;connect_client(&client);
    client_command(&client,HB_MUSE_WIRE_BEGIN,NULL,0);assert(exchange(&client,false).status==0);pump();
    sdk.begin_ack=true;sdk.phase=HB_MUSE_SDK_LISTENING;pump();
    uint8_t pcm_bytes[6]={0,0,0,0,0,0x80};client_command(&client,HB_MUSE_WIRE_PCM,pcm_bytes,6);
    assert(exchange(&client,true).status==0 && endpoint.binding.adapter.queued==1);
    hb_muse_wire_client_abandon(&client);
    assert(!client.pending && client.encoded_len==0);
    clock_ms+=2001;pump();assert(endpoint.binding.adapter.queued==0 && cancels==1);
    assert(hb_muse_wire_client_prepare_hello(&client,UINT64_C(0x83b72fe091340ca2),++clock_ms));
    assert(exchange(&client,false).status==0);
    assert(hb_muse_wire_client_prepare_open(&client,++clock_ms));
    assert(exchange(&client,false).status==HB_MUSE_WIRE_BUSY);
    sdk.phase=HB_MUSE_SDK_FENCED;pump();
    assert(hb_muse_wire_client_prepare_hello(&client,UINT64_C(0x83b72fe091340ca3),++clock_ms));
    assert(exchange(&client,false).status==0);
    assert(hb_muse_wire_client_prepare_open(&client,++clock_ms));
    assert(exchange(&client,false).status==0 && endpoint.phase==HB_MUSE_ENDPOINT_IDLE);
    assert(begins==1 && retires==1);++groups;
}
int main(void) {
    test_gates();test_ack_and_audio();test_backpressure();test_text_and_retire();
    test_cancel_fence();test_lease_reopen();test_access_revoke_and_empty_cancel();
    test_failure_and_clock();test_malformed_and_buffer();test_done_no_restart();
    test_end_drains_accepted_prefixes();test_foreign_sdk_snapshot();test_failed_text_is_not_done();
    test_sdk_rejects_local_begin();test_sample_limit();
    test_full_transport_binding_chain();test_client_abandon_remote_fence();
    printf("Muse endpoint: %u test groups passed; fake SDK, no hardware or API.\n",groups);
    return 0;
}
