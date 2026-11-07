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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/cmac.h>
typedef int hb_nfc_err_t;
typedef int nfc_iso_dep_data_t;
typedef int nfc_iso14443a_data_t;
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_AUTH -2
#define HB_NFC_ERR_PROTOCOL -3
#define HB_NFC_ERR_INTERNAL -4
#define ESP_LOGD(tag,...) ((void)(tag))
typedef struct { bool encrypt; const uint8_t *key, *iv_in; uint8_t *iv_out; } nfc_cbc_params_t;
static bool crypto_fail;
static bool nfc_aes_cbc_crypt(const nfc_cbc_params_t *p, const uint8_t *in, size_t len, uint8_t *out) {
  if (crypto_fail) return false;
  EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new(); assert(ctx);
  uint8_t tail[16]; if (len) memcpy(tail,in+len-16,16);
  int n=0,end=0;
  assert(EVP_CipherInit_ex(ctx,EVP_aes_128_cbc(),NULL,p->key,p->iv_in,p->encrypt));
  assert(EVP_CIPHER_CTX_set_padding(ctx,0));
  assert(EVP_CipherUpdate(ctx,out,&n,in,(int)len));
  assert(EVP_CipherFinal_ex(ctx,out+n,&end)); EVP_CIPHER_CTX_free(ctx);
  if (p->iv_out && len) memcpy(p->iv_out,p->encrypt ? out+len-16 : tail,16);
  return true;
}
static bool nfc_aes_ecb_encrypt(const uint8_t *key,const uint8_t *in,uint8_t *out) {
  const uint8_t iv[16]={0}; nfc_cbc_params_t p={true,key,iv,NULL};
  return nfc_aes_cbc_crypt(&p,in,16,out);
}
static int nfc_aes_cmac(const uint8_t *key,size_t key_len,const uint8_t *in,size_t len,uint8_t *out) {
  if (crypto_fail) return -1;
  CMAC_CTX *ctx=CMAC_CTX_new(); size_t n=0; assert(ctx);
  assert(CMAC_Init(ctx,key,key_len,EVP_aes_128_cbc(),NULL));
  assert(CMAC_Update(ctx,in,len)); assert(CMAC_Final(ctx,out,&n)); CMAC_CTX_free(ctx);
  return 0;
}
static int nfc_memcmp_ct(const void *a,const void *b,size_t n) { return memcmp(a,b,n); }
static size_t nfc_iso9797_pad_method1(const uint8_t *p,size_t n,uint8_t *o,size_t cap) {
  size_t size=(n+15)&~(size_t)15; if(size>cap)return 0;
  memcpy(o,p,n); memset(o+n,0,size-n); return size;
}
/* VECTORS */
static const uint8_t *expected_tx, *reply;
static size_t expected_len,reply_len;
static unsigned exchanges;
static bool is_loopback, is_listener_random, corrupt_request, corrupt_auth_reply;
static int loopback(const uint8_t *tx, size_t n, uint8_t *rx, size_t cap);
static void esp_fill_random(void *out,size_t len) { assert(len==16 || len==4);
  memcpy(out, len==4 ? ti : is_listener_random ? rnd_b : rnd_a, len); }
static int iso_dep_rats(uint8_t f,uint8_t c,nfc_iso_dep_data_t *d) { (void)f;(void)c;(void)d;return 0; }
static int iso_dep_apdu_transceive(const nfc_iso_dep_data_t *d,const uint8_t *tx,size_t n,
                                 uint8_t *rx,size_t cap,int timeout) {
  (void)d; assert(timeout>0);
  if (is_loopback) return loopback(tx,n,rx,cap);
  assert(n==expected_len); assert(memcmp(tx,expected_tx,n)==0);
  assert(reply_len<=cap); memcpy(rx,reply,reply_len); exchanges++;
  if (expected_tx==auth_first || expected_tx==auth_nonfirst) {
    bool first=expected_tx==auth_first;
    expected_tx=auth_continue; expected_len=sizeof(auth_continue);
    reply=first ? auth_reply : nonfirst_reply;
    reply_len=first ? sizeof(auth_reply) : sizeof(nonfirst_reply);
  }
  return (int)(n==sizeof(auth_first) || n==sizeof(auth_nonfirst) ? sizeof(challenge) : reply_len);
}
/* HEADER */
/* PRODUCTION */
static mfp_listener_t listener;
static unsigned writes;
static bool resolve_key(void *ctx,uint16_t address,uint8_t *out) {
  (void)ctx;if(address!=0x4004 && address!=0x4040 && address!=0x9000)return false;memcpy(out,key,16);return true;
}
static bool read_block(void *ctx,uint16_t auth,uint16_t address,uint8_t *out) {
  (void)ctx;
  if(!((auth==0x4004 && address>=8 && address<=10) ||
       (auth==0x4040 && address>=128 && address<=142) ||
       (auth==0x9000 && address>=0xb000 && address<=0xb003))) return false;
  memcpy(out,data,16);
  return true;
}
static bool write_block(void *ctx,uint16_t auth,uint16_t address,const uint8_t *value) {
  (void)ctx;if(!((auth==0x4004 && (address==8 || address==0x4004)) ||
              (auth==0x9000 && (address==0x9001 || address==0xb000))))return false;
  assert(memcmp(value,data,16)==0);writes++;return true;
}
static int loopback(const uint8_t *tx,size_t n,uint8_t *rx,size_t cap) {
  uint8_t response[MFP_LISTENER_RESPONSE_SIZE];size_t len=0;
  uint8_t request[256];
  assert(n <= sizeof(request));
  memcpy(request,tx,n);
  if(corrupt_request)request[n-1]^=1;
  is_listener_random=true;
  assert(mfp_listener_exchange(&listener,request,n,response,sizeof(response),&len)==0);
  if(corrupt_auth_reply && tx[0]==0x72 && len>1)response[len-1]^=1;
  is_listener_random=false;
  assert(len<=cap);memcpy(rx,response,len);return (int)len;
}
static void stage(const uint8_t *tx,size_t n,const uint8_t *rx,size_t m) {
  expected_tx=tx;expected_len=n;reply=rx;reply_len=m;
}
int main(void) {
  mfp_session_t s={0}; nfc_iso14443a_data_t card=0;
  assert(mfp_poller_init(&card,&s)==0);
  stage(auth_first,sizeof(auth_first),challenge,sizeof(challenge));
  assert(mfp_sl3_auth_first(&s,0x4004,key)==0);
  assert(s.authenticated && memcmp(s.ses_enc,k_enc,16)==0 && memcmp(s.ses_auth,k_mac,16)==0);
  assert(memcmp(s.ti,ti,4)==0);
  uint8_t out[256]; size_t len=99;
  stage(read_plain,sizeof(read_plain),plain_reply,sizeof(plain_reply));
  assert(mfp_sl3_read(&s,8,1,false,out,sizeof(out),&len)==0 && len==16 && memcmp(out,data,16)==0);
  stage(read_enc,sizeof(read_enc),enc_reply,sizeof(enc_reply));
  assert(mfp_sl3_read(&s,8,1,true,out,sizeof(out),&len)==0 && s.read_counter==2 && memcmp(out,data,16)==0);
  stage(write_enc,sizeof(write_enc),write_reply,sizeof(write_reply));
  assert(mfp_sl3_write(&s,8,true,data,16)==0 && s.write_counter==1);
  stage(write_plain,sizeof(write_plain),write_reply2,sizeof(write_reply2));
  assert(mfp_sl3_write(&s,8,false,data,16)==0 && s.write_counter==2);
  stage(auth_nonfirst,sizeof(auth_nonfirst),challenge,sizeof(challenge));
  assert(mfp_sl3_auth_nonfirst(&s,0x4004,key)==0 && s.read_counter==2 && s.write_counter==2);
  uint8_t invalid[sizeof(bad_reply)];memcpy(invalid,bad_reply,sizeof(invalid));invalid[sizeof(invalid)-1]^=1;
  memset(out,0xab,sizeof(out)); stage(read_bad,sizeof(read_bad),invalid,sizeof(invalid));
  assert(mfp_sl3_read(&s,8,1,false,out,sizeof(out),&len)==HB_NFC_ERR_AUTH);
  assert(!s.authenticated && len==0 && out[0]==0xab);
  assert(mfp_sl3_auth_nonfirst(&s,0x4004,key)==HB_NFC_ERR_AUTH);
  s.authenticated=true;s.read_counter=UINT16_MAX;
  unsigned previous=exchanges;
  assert(mfp_sl3_read(&s,8,1,false,out,sizeof(out),&len)==HB_NFC_ERR_AUTH && exchanges==previous);
  s.authenticated=true;s.write_counter=UINT16_MAX;
  assert(mfp_sl3_write(&s,8,false,data,16)==HB_NFC_ERR_AUTH && exchanges==previous);
  assert(mfp_sl3_write(&s,8,false,data,512)==HB_NFC_ERR_PARAM);
  assert(mfp_sl3_read(&s,8,16,false,out,sizeof(out),&len)==HB_NFC_ERR_PARAM);
  crypto_fail=true;
  uint8_t a[16],b[16]; memset(a,0xaa,16); memset(b,0xbb,16);
  assert(mfp_sl3_derive_session_keys(key,rnd_a,rnd_b,a,b)==HB_NFC_ERR_INTERNAL && a[0]==0xaa && b[0]==0xbb);
  crypto_fail=false;
  mfp_listener_config_t config={.key=resolve_key,.read=read_block,.write=write_block};
  assert(mfp_listener_init(&listener,&config)==0);
  is_loopback=true;
  assert(mfp_poller_init(&card,&s)==0);
  assert(mfp_sl3_auth_first(&s,0x4004,key)==0);
  assert(mfp_sl3_read(&s,8,1,false,out,sizeof(out),&len)==0 && memcmp(out,data,16)==0);
  assert(mfp_sl3_read(&s,8,1,true,out,sizeof(out),&len)==0 && memcmp(out,data,16)==0);
  assert(mfp_sl3_write(&s,8,true,data,16)==0 && writes==1);
  assert(mfp_sl3_write(&s,8,false,data,16)==0 && writes==2);
  assert(mfp_sl3_auth_nonfirst(&s,0x4004,key)==0 && listener.session.read_counter==2);
  assert(mfp_sl3_read(&s,8,1,true,out,sizeof(out),&len)==0);
  mfp_listener_reset(&listener);
  assert(!listener.session.authenticated && !listener.has_challenge);
  assert(mfp_sl3_read(&s,8,1,true,out,sizeof(out),&len)!=0 && !s.authenticated);
  corrupt_auth_reply=true;
  assert(mfp_sl3_auth_first(&s,0x4004,key)!=0 && !s.authenticated);
  corrupt_auth_reply=false;
  assert(mfp_sl3_auth_first(&s,0x4004,key)==0);
  unsigned writes_before=writes;
  corrupt_request=true;
  assert(mfp_sl3_write(&s,8,true,data,16)!=0 && writes==writes_before);
  assert(!listener.session.authenticated && !s.authenticated);
  corrupt_request=false;
  assert(mfp_sl3_auth_first(&s,0x4004,key)==0);
  assert(mfp_sl3_read(&s,8,1,true,out,sizeof(out),&len)==0);
  mfp_listener_reset(&listener);
  assert(mfp_sl3_auth_first(&s,0x4004,key)==0);
  assert(mfp_listener_exchange(&listener,multi_plain,sizeof(multi_plain),out,sizeof(out),&len)==0);
  assert(len==sizeof(multi_plain_reply) && !memcmp(out,multi_plain_reply,len));
  assert(mfp_listener_exchange(&listener,multi_enc,sizeof(multi_enc),out,sizeof(out),&len)==0);
  assert(len==sizeof(multi_enc_reply) && !memcmp(out,multi_enc_reply,len));
  mfp_listener_reset(&listener);assert(mfp_sl3_auth_first(&s,0x4004,key)==0);
  assert(mfp_sl3_read(&s,8,3,true,out,sizeof(out),&len)==0 && len==48);
  for(unsigned i=0;i<3;i++)assert(!memcmp(out+i*16,data,16));
  assert(mfp_sl3_read(&s,8,5,true,out,sizeof(out),&len)!=0);
  assert(mfp_sl3_auth_first(&s,0x4040,key)==0);
  assert(mfp_sl3_read(&s,128,15,true,out,sizeof(out),&len)==0 && len==240);
  for(unsigned i=0;i<15;i++)assert(!memcmp(out+i*16,data,16));
  uint8_t version[MFP_VERSION_SIZE],signature[MFP_SIGNATURE_SIZE];
  for(size_t i=0;i<sizeof(version);i++)version[i]=(uint8_t)i;
  memset(signature,0x5a,sizeof(signature));
  config.version=version;config.signature=signature;
  assert(mfp_listener_init(&listener,&config)==0);
  assert(mfp_get_version(&s,out)==0 && !memcmp(out,version,sizeof(version)));
  assert(mfp_read_signature(&s,out)==0 && !memcmp(out,signature,sizeof(signature)));
  assert(mfp_sl3_auth_first(&s,0x9000,key)==0);
  assert(mfp_sl3_read(&s,0xb000,4,true,out,sizeof(out),&len)==0 && len==64);
  assert(mfp_sl3_write(&s,0xb000,false,data,16)==0);
  assert(mfp_sl3_write(&s,0x9001,true,data,16)==0);
  unsigned key_writes=writes;
  assert(mfp_sl3_write(&s,0x9001,false,data,16)==HB_NFC_ERR_PARAM && writes==key_writes);
  assert(mfp_sl3_auth_first(&s,0x4004,key)==0);
  assert(mfp_sl3_write(&s,0x4004,true,data,16)==0);
  assert(mfp_sl3_auth_first(&s,0x9002,key)!=0 && !s.authenticated);
  uint8_t v[]={0x90,0x60,0,0,0};
  assert(mfp_listener_exchange(&listener,v,sizeof(v),out,sizeof(out),&len)==0);
  assert(len==9 && out[7]==0x91 && out[8]==0xaf && memcmp(out,version,7)==0);
  v[1]=0xaf;
  assert(mfp_listener_exchange(&listener,v,sizeof(v),out,sizeof(out),&len)==0 && len==9);
  assert(memcmp(out,version+7,7)==0);
  assert(mfp_listener_exchange(&listener,v,sizeof(v),out,sizeof(out),&len)==0 && len==16);
  assert(out[15]==0x90 && memcmp(out,version+14,14)==0);
  assert(mfp_listener_exchange(&listener,v,sizeof(v),out,sizeof(out),&len)==0 && len==2 && out[1]==0x0b);
  const uint8_t sig[]={0x3c,0};
  assert(mfp_listener_exchange(&listener,sig,sizeof(sig),out,sizeof(out),&len)==0 && len==57);
  assert(memcmp(out+1,signature,56)==0);
  assert(mfp_listener_exchange(&listener,auth_first,sizeof(auth_first),out,1,&len)==HB_NFC_ERR_PARAM);
  assert(!listener.has_challenge && len==0);
  uint8_t malformed[]={0x90,0x70,0,0,32};
  assert(mfp_listener_exchange(&listener,malformed,sizeof(malformed),out,sizeof(out),&len)==HB_NFC_ERR_PROTOCOL);
  puts("MIFARE Plus: independent AES/CMAC wire vectors, counters, failures and bounds OK");
}
