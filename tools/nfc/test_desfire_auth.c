// Copyright (c) 2025 HIGH CODE LLC
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
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/cmac.h>
#include "firmware_crypto.h"
#include "mf_desfire.h"
#include "nfc_crypto.h"

static unsigned s_checks, s_failures, s_calls, s_random_pos, s_crypto_calls, s_fail_crypto;
static int s_mode, s_fault;
static bool s_wrong_key;
static int64_t s_time, s_time_step;
int64_t esp_timer_get_time(void) {int64_t now=s_time;s_time+=s_time_step;return now;}
static uint8_t s_rnda[16], s_rndb[16], s_challenge[16], s_answer[32], s_enc[16], s_mac[16];
static uint8_t s_second[32], s_ti[4];
static uint8_t s_last_apdu[300];
static size_t s_last_len;
#define CHECK(c) do { s_checks++; if (!(c)) { s_failures++; fprintf(stderr,"FAIL line %d: %s\n", __LINE__, #c); } } while (0)

enum {MODE_FIRST, MODE_NONFIRST, MODE_TIMEOUT, MODE_CHAIN, MODE_OVERFLOW, MODE_OK, MODE_BAD_STATUS, MODE_VERSION, MODE_OVERSIZE, MODE_SHORT, MODE_BAD_SW, MODE_NATIVE};

static void hex(const char *str, uint8_t *out) {
  for (size_t i=0; i<strlen(str)/2; ++i) {
    unsigned value=0; if (sscanf(str+2*i,"%2x",&value)!=1) abort(); out[i]=(uint8_t)value;
  }
}
static bool zero(const void *data, size_t len) {
  const uint8_t *p=data; for (size_t i=0;i<len;i++) if(p[i]) return false; return true;
}
static bool crypto(bool encrypt, bool cbc, const uint8_t *key, const uint8_t *iv,
                   const uint8_t *in, size_t len, uint8_t *out) {
  EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new(); int n=0, final=0;
  if(!ctx) return false;
  bool ok=EVP_CipherInit_ex(ctx,cbc?EVP_aes_128_cbc():EVP_aes_128_ecb(),NULL,key,iv,encrypt)==1;
  ok=ok && EVP_CIPHER_CTX_set_padding(ctx,0)==1 &&
     EVP_CipherUpdate(ctx,out,&n,in,(int)len)==1 && EVP_CipherFinal_ex(ctx,out+n,&final)==1;
  EVP_CIPHER_CTX_free(ctx); return ok && (size_t)(n+final)==len;
}
int firmware_crypto_cipher(const firmware_crypto_cipher_t *p,firmware_crypto_span_t in,uint8_t *out) {
  if (++s_crypto_calls==s_fail_crypto) {memset(out,0xAB,in.len);return -1;}
  return crypto(p->is_encrypt,p->algorithm==PSA_ALG_CBC_NO_PADDING,p->key.ptr,p->iv.ptr,in.ptr,in.len,out)?0:-1;
}
int firmware_crypto_mac(firmware_crypto_span_t key,firmware_crypto_span_t msg,int alg,uint8_t *out) {
  (void)alg; if (++s_crypto_calls==s_fail_crypto) {memset(out,0xAB,16);return -1;}
  CMAC_CTX *ctx=CMAC_CTX_new(); size_t n=0; if(!ctx) return -1;
  bool ok=CMAC_Init(ctx,key.ptr,key.len,EVP_aes_128_cbc(),NULL)==1 &&
          CMAC_Update(ctx,msg.ptr,msg.len)==1 && CMAC_Final(ctx,out,&n)==1;
  CMAC_CTX_free(ctx); return ok && n==16?0:-1;
}
uint32_t esp_random(void) { uint32_t n; memcpy(&n,s_rnda+(s_random_pos%16),4); s_random_pos+=4; return n; }
hb_nfc_err_t iso_dep_rats(int fsdi,int cid,nfc_iso_dep_data_t *dep) { (void)fsdi;(void)cid;(void)dep;return HB_NFC_OK; }
int iso_dep_apdu_transceive(nfc_iso_dep_data_t *dep,const uint8_t *apdu,size_t len,
                          uint8_t *rx,size_t max,int timeout) {
  (void)dep; CHECK(timeout>=0 && timeout<=5000); s_calls++; s_last_len=len;
  if(len>sizeof(s_last_apdu)) abort(); memcpy(s_last_apdu,apdu,len);
  if(s_mode==MODE_NATIVE) {
    CHECK(len == 1 && apdu[0] == (s_calls == 1 ? 0x60 : 0xAF));
    rx[0] = s_calls == 1 ? 0xAF : 0;
    rx[1] = (uint8_t)s_calls;
    return 2;
  }
  if(s_mode==MODE_TIMEOUT) return 0;
  if(s_mode==MODE_OVERSIZE) return (int)max+1;
  if(s_mode==MODE_SHORT) {rx[0]=0x91;return 1;}
  if(s_mode==MODE_BAD_SW) {rx[0]=0x90;rx[1]=0;return 2;}
  if(s_mode==MODE_VERSION) {
    CHECK(apdu[1]==(s_calls==1?0x60:0xAF) && len==5);
    size_t n=s_calls<3?7:14;
    for(size_t i=0;i<n;i++) rx[i]=(uint8_t)((s_calls-1)*7+i);
    rx[n]=0x91;rx[n+1]=s_calls<3?0xAF:0;
    return (int)n+2;
  }
  if(s_mode==MODE_OK || s_mode==MODE_BAD_STATUS) { rx[0]=0x91;rx[1]=s_mode==MODE_OK?0:0xAE;return 2; }
  if(s_mode==MODE_CHAIN) {
    if(s_calls>64) return 0; /* Old implementation must not hang the regression. */
    rx[0]=0x91;rx[1]=0xAF;return 2;
  }
  if(s_mode==MODE_OVERFLOW) { memset(rx,0x42,10);rx[10]=0x91;rx[11]=0;return 12; }
  if(s_calls==1) {
    CHECK(len==(s_mode==MODE_FIRST?8U:7U));
    CHECK(apdu[0]==0x90 && apdu[1]==(s_mode==MODE_FIRST?0x71:0x77) && apdu[4]==(s_mode==MODE_FIRST?2:1));
    memcpy(rx,s_challenge,16);rx[16]=0x91;rx[17]=0xAF;
    return s_fault==-1?1:18;
  }
  CHECK(s_calls==2); CHECK(len==38 && apdu[1]==0xAF);
  if(s_wrong_key) {
    CHECK(memcmp(apdu+5,s_second,32)!=0);rx[0]=0x91;rx[1]=0xAE;return 2;
  }
  CHECK(memcmp(apdu+5,s_second,32)==0);
  size_t n=s_mode==MODE_FIRST?32:16;
  CHECK(n+2<=max); memcpy(rx,s_answer,n);
  if(s_fault>0 && s_fault<=(int)n) rx[s_fault-1]^=1;
  rx[n]=0x91; rx[n+1]=0; return (int)n+2;
}
static mf_desfire_session_t session(int mode) {
  mf_desfire_session_t s={0}; s.dep_ready=true; s_mode=mode; s_fault=0;
  s_calls=s_random_pos=s_crypto_calls=s_fail_crypto=0; s_time=s_time_step=0; s_wrong_key=false; return s;
}
static void stale(mf_desfire_session_t *s) {
  s->authenticated=s->ev2_authenticated=true; s->key_no=7; s->key_type=MF_DESFIRE_KEY_AES;
  memset(s->session_key,0xAB,16); memset(s->iv,0xAB,16); memset(s->ev2_ses_mac,0xAB,16);
  memset(s->ev2_ses_enc,0xAB,16); memcpy(s->ev2_ti,s_ti,4); s->ev2_cmd_ctr=0x1234;
  memset(s->ev2_pd_cap2,0xAB,6);memset(s->ev2_pcd_cap2,0xAB,6);
}
static bool revoked(const mf_desfire_session_t *s) {
  return !s->authenticated && !s->ev2_authenticated && zero(s->session_key,16) && zero(s->iv,16) &&
         zero(s->ev2_ses_mac,16) && zero(s->ev2_ses_enc,16) && zero(s->ev2_ti,4) && !s->ev2_cmd_ctr;
}
static void vectors(void) {
  /* NXP AN12343 Rev1.1 Table16: published, synthetic zero-key transcript. */
  hex("B04D0787C93EE0CC8CACC8E86F16C6FE",s_rnda);
  hex("FA659AD0DCA738DD65DC7DC38612AD81",s_rndb);
  hex("24677DDBD46349E623798FD729006E79",s_challenge);
  hex("3B50445F21D21D77D500794DEB245E5A754F5F901844259F4C9B31A5C7335ACD",s_second);
  hex("04C6DBD67417ED0D31DDDE4D2E3FFAC2B4B074F638EEF7FFF9254963B65C7759",s_answer);
  hex("63DC07286289A7A6C0334CA31C314A04",s_enc);
  hex("774F26743ECE6AF5033B6AE8522946F6",s_mac); hex("8CF141F3",s_ti);
}
static void kdf_iv(void) {
  uint8_t key[16]={0}, enc[16], mac[16], iv[16], expected[16], in[16]={0};
  s_fail_crypto=s_crypto_calls=0;
  nfc_ev2_derive_session_keys(key,s_rnda,s_rndb,mac,enc);
  CHECK(memcmp(enc,s_enc,16)==0);CHECK(memcmp(mac,s_mac,16)==0);
  /* AN12343 section7.1.3, fixed IV input, independent OpenSSL AES oracle. */
  hex("A55A8CF141F33412000000000000000000",in);
  CHECK(crypto(true,false,s_enc,NULL,in,16,expected));
  nfc_ev2_build_iv_cmd(s_enc,s_ti,0x1234,iv); CHECK(memcmp(iv,expected,16)==0);
  in[0]=0x5A;in[1]=0xA5;CHECK(crypto(true,false,s_enc,NULL,in,16,expected));
  nfc_ev2_build_iv_resp(s_enc,s_ti,0x1234,iv);CHECK(memcmp(iv,expected,16)==0);
}
static void authentication(void) {
  uint8_t key[16]={0}; mf_desfire_session_t s=session(MODE_FIRST);
  CHECK(mf_desfire_authenticate_ev2_first(&s,0,key)==HB_NFC_OK);
  CHECK(s.authenticated && s.ev2_authenticated && memcmp(s.ev2_ti,s_ti,4)==0 && !s.ev2_cmd_ctr);
  CHECK(memcmp(s.ev2_ses_enc,s_enc,16)==0 && memcmp(s.ev2_ses_mac,s_mac,16)==0);
  for(int fault=1;fault<=32;fault++) {
    s=session(MODE_FIRST); stale(&s); s_fault=fault;
    CHECK(mf_desfire_authenticate_ev2_first(&s,0,key)!=HB_NFC_OK);CHECK(revoked(&s));
  }
  for(unsigned fail=1;fail<=5;fail++) {
    s=session(MODE_FIRST);stale(&s);s_fail_crypto=fail;
    CHECK(mf_desfire_authenticate_ev2_first(&s,0,key)!=HB_NFC_OK);CHECK(revoked(&s));
  }
  s=session(MODE_FIRST);stale(&s);s_fault=-1;
  CHECK(mf_desfire_authenticate_ev2_first(&s,0,key)!=HB_NFC_OK);CHECK(revoked(&s));
  /* NonFirst returns RndA' only; retains transaction ID and command counter. */
  uint8_t rot[16], iv[16]={0};memcpy(rot,s_rnda+1,15);rot[15]=s_rnda[0];
  CHECK(crypto(true,true,key,iv,rot,16,s_answer));
  s=session(MODE_NONFIRST);stale(&s);
  CHECK(mf_desfire_authenticate_ev2_nonfirst(&s,0,key)==HB_NFC_OK);
  CHECK(s.authenticated && s.ev2_authenticated && s.ev2_cmd_ctr==0x1234 && !memcmp(s.ev2_ti,s_ti,4));
  CHECK(!memcmp(s.ev2_ses_enc,s_enc,16) && !memcmp(s.ev2_ses_mac,s_mac,16));
  for(int fault=1;fault<=16;fault++) {
    s=session(MODE_NONFIRST);stale(&s);s_fault=fault;
    CHECK(mf_desfire_authenticate_ev2_nonfirst(&s,0,key)!=HB_NFC_OK);CHECK(revoked(&s));
  }
  s=session(MODE_NONFIRST);
  CHECK(mf_desfire_authenticate_ev2_nonfirst(&s,0,key)==HB_NFC_ERR_AUTH && !s_calls);
  typedef hb_nfc_err_t (*auth_fn)(mf_desfire_session_t*,uint8_t,const uint8_t*);
  auth_fn funcs[]={mf_desfire_authenticate_ev1_3des,mf_desfire_authenticate_ev1_aes,
                   mf_desfire_authenticate_ev2_first,mf_desfire_authenticate_ev2_nonfirst};
  for(unsigned i=0;i<sizeof(funcs)/sizeof(funcs[0]);i++) {
    s=session(MODE_TIMEOUT);stale(&s);
    CHECK(funcs[i](&s,0,key)!=HB_NFC_OK);CHECK(revoked(&s));
  }
  mf_desfire_aid_t aid={{0}};s=session(MODE_OK);stale(&s);
  CHECK(mf_desfire_select_application(&s,&aid)==HB_NFC_OK);CHECK(revoked(&s));
  vectors();
  s=session(MODE_FIRST);stale(&s);s_wrong_key=true;key[0]=1;
  CHECK(mf_desfire_authenticate_ev2_first(&s,0,key)!=HB_NFC_OK && revoked(&s));
}
static bool cancel_transfer(void) { return true; }
static void framing(void) {
  uint8_t out[512], data[256]={0};size_t n=99;uint16_t sw=0xFFFF;
  mf_desfire_session_t s=session(MODE_NATIVE);
  s.use_native_frames=true;
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,sizeof(out),&n,&sw)==HB_NFC_OK);
  CHECK(n==2 && out[0]==1 && out[1]==2 && sw==MF_DESFIRE_SW_OK && s_calls==2);
  s=session(MODE_NATIVE);s.use_native_frames=true;s.should_cancel=cancel_transfer;stale(&s);
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,sizeof(out),&n,&sw)!=HB_NFC_OK);
  CHECK(n==0 && s_calls==0 && revoked(&s) && s.use_native_frames && s.should_cancel==cancel_transfer);
  s=session(MODE_OK);
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,sizeof(out),&n,&sw)==HB_NFC_OK);
  CHECK(s_last_len==5 && !memcmp(s_last_apdu,"\x90\x60\x00\x00\x00",5));
  s=session(MODE_OK);
  CHECK(mf_desfire_transceive_native(&s,0x60,data,256,out,sizeof(out),&n,&sw)==HB_NFC_ERR_PARAM && !s_calls);
  s=session(MODE_OK);
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,1,out,sizeof(out),&n,&sw)==HB_NFC_ERR_PARAM && !s_calls);
  s=session(MODE_OVERFLOW);
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,4,&n,&sw)!=HB_NFC_OK);
  CHECK(n==0);
  s=session(MODE_CHAIN);
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,sizeof(out),&n,&sw)!=HB_NFC_OK);
  CHECK(s_calls<=32 && n==0);
  s=session(MODE_OK);
  CHECK(mf_desfire_write_data(&s,0,0,data,248)==HB_NFC_OK && s_last_apdu[4]==255);
  s=session(MODE_OK);
  CHECK(mf_desfire_write_data(&s,0,0,data,249)==HB_NFC_ERR_PARAM && !s_calls);
  s=session(MODE_OK);
  CHECK(mf_desfire_read_data(&s,0,0x1000000,1,out,sizeof(out),&n)==HB_NFC_ERR_PARAM && !s_calls);
  s=session(MODE_OK);
  CHECK(mf_desfire_read_data(&s,0,0,0x1000000,out,sizeof(out),&n)==HB_NFC_ERR_PARAM && !s_calls);
  s=session(MODE_OK);
  CHECK(mf_desfire_write_data(&s,0,0x1000000,data,1)==HB_NFC_ERR_PARAM && !s_calls);
  s=session(MODE_BAD_STATUS);stale(&s);
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,sizeof(out),&n,&sw)==HB_NFC_OK && sw==0x91AE);
  CHECK(revoked(&s));
}
static void robustness(void) {
  uint8_t key[16]={0}, out[512], mac[8], enc[16], cmac[16], iv[16];
  size_t n;uint16_t sw;mf_desfire_session_t s;
  mf_desfire_version_t ver;
  s=session(MODE_VERSION);
  CHECK(mf_desfire_get_version(&s,&ver)==HB_NFC_OK && s_calls==3);
  for(unsigned i=0;i<28;i++) CHECK(((uint8_t*)&ver)[i]==i);
  const int modes[]={MODE_OVERSIZE,MODE_SHORT,MODE_BAD_SW, MODE_NATIVE};
  for(unsigned i=0;i<3;i++) {
    s=session(modes[i]);stale(&s);n=42;
    CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,sizeof(out),&n,&sw)!=HB_NFC_OK);
    CHECK(n==0 && revoked(&s));
    s=session(modes[i]);stale(&s);
    CHECK(mf_desfire_authenticate_ev2_first(&s,0,key)!=HB_NFC_OK && revoked(&s));
  }
  s=session(MODE_CHAIN);s_time_step=7000000;
  CHECK(mf_desfire_transceive_native(&s,0x60,NULL,0,out,sizeof(out),&n,&sw)!=HB_NFC_OK && s_calls<=2);
  s=session(MODE_OK);
  CHECK(mf_desfire_transceive_native(&s,0x60,out,SIZE_MAX,out,sizeof(out),&n,&sw)==HB_NFC_ERR_PARAM && !s_calls);
  typedef hb_nfc_err_t (*auth_fn)(mf_desfire_session_t*,uint8_t,const uint8_t*);
  auth_fn funcs[]={mf_desfire_authenticate_ev1_3des,mf_desfire_authenticate_ev1_aes,
                   mf_desfire_authenticate_ev2_first,mf_desfire_authenticate_ev2_nonfirst};
  for(unsigned i=0;i<4;i++) {
    s=session(MODE_OK);stale(&s);s.dep_ready=false;
    CHECK(funcs[i](&s,0,key)==HB_NFC_ERR_PARAM && revoked(&s) && !s_calls);
    s=session(MODE_OK);stale(&s);
    CHECK(funcs[i](&s,0,NULL)==HB_NFC_ERR_PARAM && revoked(&s) && !s_calls);
  }
  for(unsigned fail=1;fail<=2;fail++) {
    s_fail_crypto=fail;s_crypto_calls=0;memset(enc,0xAB,16);memset(cmac,0xAB,16);
    CHECK(!nfc_ev2_derive_session_keys(key,s_rnda,s_rndb,cmac,enc));
    CHECK(zero(enc,16) && zero(cmac,16));
  }
  s_fail_crypto=s_crypto_calls=0;
  nfc_ev2_mac_ctx_t ctx={.ses_mac_key=key,.ti=s_ti,.ins=0xBD,.cmd_ctr=1};
  nfc_ev2_cmd_data_t cmd={.header=NULL,.hdr_len=1};
  CHECK(nfc_ev2_compute_cmd_mac(&ctx,&cmd,mac)!=0);
  cmd=(nfc_ev2_cmd_data_t){.header=out,.hdr_len=SIZE_MAX};
  CHECK(nfc_ev2_compute_cmd_mac(&ctx,&cmd,mac)!=0);
  cmd=(nfc_ev2_cmd_data_t){.data=out,.data_len=SIZE_MAX};
  CHECK(nfc_ev2_compute_cmd_mac(&ctx,&cmd,mac)!=0);
  CHECK(nfc_ev2_compute_resp_mac(&ctx,NULL,1,mac)!=0);
  CHECK(nfc_ev2_compute_resp_mac(&ctx,out,SIZE_MAX,mac)!=0);
  s_crypto_calls=0;s_fail_crypto=1;
  CHECK(!nfc_ev2_build_iv_cmd(s_enc,s_ti,0,iv) && zero(iv,16));
  s_crypto_calls=0;s_fail_crypto=1;
  CHECK(!nfc_ev2_build_iv_resp(s_enc,s_ti,0,iv) && zero(iv,16));
}

int main(void) {
  vectors();kdf_iv();authentication();framing();robustness();
  printf("DESFire/EV2 production host: %u checks, %u failures (OpenSSL, NXP AN12343; RF/PSA not exercised)\n",s_checks,s_failures);
  return s_failures?1:0;
}
