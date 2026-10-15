#include "hb_muse_rpc.h"
#include "../transport/hb_muse_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {
    bool present, ensure_ok, enable_ok, connect_ok, submit_ok, reply_ok;
    unsigned ensure, enable, connect, revoke, status, submit, pull;
    size_t reply_size;
    hb_muse_runtime_status_t s;
} f;
static bool ensure(void *u) { assert(u==&f); ++f.ensure; return f.ensure_ok; }
static bool enable(void *u,bool e) { assert(u==&f && e); ++f.enable; return f.enable_ok; }
static bool connect(void *u) { assert(u==&f); ++f.connect; return f.connect_ok; }
static void revoke(void *u) { assert(u==&f); ++f.revoke; }
static bool status(void *u,hb_muse_runtime_status_t *s) { assert(u==&f); ++f.status; *s=f.s; return f.present; }
static bool submit(void *u,const uint8_t *p,size_t n) { assert(u==&f && p && n); ++f.submit; return f.submit_ok; }
static bool pull(void *u,uint8_t *p,size_t c,size_t *n) {
    assert(u==&f && p && c>=248); ++f.pull; *n=f.reply_size;
    if (f.reply_size<=c) memset(p,0xab,f.reply_size);
    return f.reply_ok;
}
int main(void) {
    hb_muse_rpc_t rpc={0}; uint8_t in[248]={1,1},out[248]; size_t n=99;
    hb_muse_rpc_ops_t ops={ensure,enable,connect,revoke,status,submit,pull};
    assert(!hb_muse_rpc_init(NULL,&ops,&f));
    hb_muse_rpc_ops_t bad=ops; bad.connect=NULL;
    assert(!hb_muse_rpc_init(&rpc,&bad,&f));
    assert(hb_muse_rpc_init(&rpc,&ops,&f)); assert(!f.ensure && !f.status);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_ENABLE,in,2,out,247,&n)==HB_MUSE_RPC_INVALID && !n && !f.ensure);
    assert(hb_muse_rpc_execute(&rpc,0x1201,in,1,out,248,&n)==HB_MUSE_RPC_UNSUPPORTED);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_STATUS,in,1,out,248,&n)==HB_MUSE_RPC_OK);
    assert(n==32 && out[0]==1 && out[3]==0 && out[28]==248 && !f.ensure && !f.connect);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL,in,1,out,248,&n)==HB_MUSE_RPC_BUSY && !f.pull);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_ENABLE,in,2,out,248,&n)==HB_MUSE_RPC_ERROR && f.ensure==1 && !f.enable);
    f.ensure_ok=true;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_ENABLE,in,2,out,248,&n)==HB_MUSE_RPC_BUSY && f.enable==1);
    f.enable_ok=true;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_ENABLE,in,2,out,248,&n)==HB_MUSE_RPC_OK && !n && !f.connect);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_CONNECT,in,1,out,248,&n)==HB_MUSE_RPC_BUSY && f.connect==1);
    f.connect_ok=true;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_CONNECT,in,1,out,248,&n)==HB_MUSE_RPC_OK && f.connect==2);
    f.present=true; f.s=(hb_muse_runtime_status_t){.phase=HB_MUSE_RUNTIME_CONNECTING,.enabled=true,.paired=true,.network=true,
        .cloud_authorized=true,.worker_started=true,.session_requested_revision=0x78563412,.session_applied_revision=3,
        .accepted_frames=7,.rejected_frames=8,.processed_frames=9};
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_STATUS,in,1,out,248,&n)==HB_MUSE_RPC_OK);
    assert(n==32 && out[3]==159 && out[4]==0x12 && out[7]==0x78 && out[8]==3 && out[12]==7 && out[20]==9);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL,in,1,out,248,&n)==HB_MUSE_RPC_OK && !n && f.pull==1);
    f.reply_ok=true; f.reply_size=248;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL,in,1,out,248,&n)==HB_MUSE_RPC_OK && n==248 && out[247]==0xab);
    f.reply_size=249;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL,in,1,out,248,&n)==HB_MUSE_RPC_ERROR && !n && f.revoke==1);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_TX,in,1,out,248,&n)==HB_MUSE_RPC_INVALID && !f.submit);
    hb_muse_wire_frame_t frame={.opcode=HB_MUSE_WIRE_HELLO,.payload_len=8}; frame.payload[0]=1;
    size_t wire_size; assert(hb_muse_wire_encode(&frame,in,sizeof(in),&wire_size));
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_TX,in,wire_size,out,248,&n)==HB_MUSE_RPC_BUSY && f.submit==1);
    f.submit_ok=true;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_TX,in,wire_size,out,248,&n)==HB_MUSE_RPC_OK && !n && f.submit==2);
    frame.flags=HB_MUSE_WIRE_RESPONSE;
    assert(hb_muse_wire_encode(&frame,in,sizeof(in),&wire_size));
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_TX,in,wire_size,out,248,&n)==HB_MUSE_RPC_INVALID && f.submit==2);
    in[0]=1; in[1]=2;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_ENABLE,in,2,out,248,&n)==HB_MUSE_RPC_INVALID);
    in[1]=0;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_ENABLE,in,2,out,248,&n)==HB_MUSE_RPC_OK && f.revoke==2);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_REVOKE,in,1,out,248,&n)==HB_MUSE_RPC_OK && f.revoke==3);
    in[0]=2;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_REVOKE,in,1,out,248,&n)==HB_MUSE_RPC_INVALID && f.revoke==3);
    /* Loss simulation: the first returned frame is discarded by the master;
     * retries must not consume, connect, submit audio or initialize anything. */
    assert(hb_muse_rpc_init(&rpc,&ops,&f));
    f.s.enabled=true;f.s.cloud_authorized=true;f.reply_ok=true;f.reply_size=248;
    uint8_t req[5]={1,1,0,0,0}, replay[252], saved[252];
    unsigned before=f.pull;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,251,&n)==HB_MUSE_RPC_INVALID&&f.pull==before);
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK&&n==252&&f.pull==before+1);
    memcpy(saved,replay,sizeof saved);
    for(unsigned i=0;i<10;++i) {
        assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK);
        assert(n==252&&!memcmp(saved,replay,n)&&f.pull==before+1);
    }
    req[1]=3;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_INVALID&&f.pull==before+1);
    req[1]=2; f.reply_ok=false;f.reply_size=0;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK&&n==4&&f.pull==before+2);
    f.reply_ok=true;f.reply_size=17;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK&&n==4&&f.pull==before+2);
    req[1]=1;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_INVALID);
    req[1]=3;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK&&n==21&&f.pull==before+3);
    f.s.enabled=false;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_BUSY);
    for(size_t i=0;i<sizeof rpc.pull_cache;++i) assert(!rpc.pull_cache[i]);
    f.s.enabled=true;req[1]=1;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK);
    ++f.s.session_requested_revision;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_BUSY&&!n);
    req[1]=0;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_INVALID);
    assert(hb_muse_rpc_init(&rpc,&ops,&f));req[1]=1;f.s.cloud_authorized=false;before=f.pull;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK&&n==4&&f.pull==before);
    ++f.s.session_requested_revision;req[1]=2;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK&&n==4&&f.pull==before);
    ++f.s.session_requested_revision;f.s.cloud_authorized=true;req[1]=3;
    assert(hb_muse_rpc_execute(&rpc,HB_MUSE_RPC_PULL_REPLAY,req,5,replay,sizeof replay,&n)==HB_MUSE_RPC_OK&&n==21&&f.pull==before+1);
    puts("Muse RPC: replay/loss/empty/sequence/fence/capacity checks and legacy strict C11/UBSan passed; no hardware.");
}
