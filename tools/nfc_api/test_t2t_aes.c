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
#include <string.h>
#include <stdio.h>
#include <openssl/evp.h>
#include <openssl/cmac.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_RESPONSE -3
#define ESP_LOGE(tag,...) ((void)(tag))
typedef struct { bool encrypt; const uint8_t *key; const uint8_t *iv_in; uint8_t *iv_out; } nfc_cbc_params_t;
static bool fail_crypto;
static bool nfc_aes_cbc_crypt(const nfc_cbc_params_t *p,const uint8_t *in,size_t n,uint8_t *out) {
  if(fail_crypto)return false;
  EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();assert(ctx);
  int used=0,end=0;
  assert(EVP_CipherInit_ex(ctx,EVP_aes_128_cbc(),NULL,p->key,p->iv_in,p->encrypt));
  EVP_CIPHER_CTX_set_padding(ctx,0);
  assert(EVP_CipherUpdate(ctx,out,&used,in,n));
  assert(EVP_CipherFinal_ex(ctx,out+used,&end));
  EVP_CIPHER_CTX_free(ctx);return true;
}
static int nfc_aes_cmac(const uint8_t *key,size_t key_len,const uint8_t *data,size_t n,uint8_t *out) {
  if(fail_crypto)return -1;
  CMAC_CTX *ctx=CMAC_CTX_new();assert(ctx);size_t length=0;
  assert(CMAC_Init(ctx,key,key_len,EVP_aes_128_cbc(),NULL));
  assert(CMAC_Update(ctx,data,n));assert(CMAC_Final(ctx,out,&length));
  CMAC_CTX_free(ctx);return 0;
}
static int nfc_memcmp_ct(const void *a,const void *b,size_t n){return CRYPTO_memcmp(a,b,n);}
/* HEADER */
/* PRODUCTION */
/* VECTORS */
/* CARD TYPES */
typedef struct { t2t_emu_config_t config;uint8_t memory[924];uint16_t length; } nfc_t2t_file_t;
/* ENGINE DEFINES */
#define HB_NFC_OK ESP_OK
static uint8_t s_mem[240],s_aes_key_number;
static uint16_t s_aes_key_known[2];
static uint32_t s_failed_auth_attempts;
static t2t_aes_session_t s_aes_session;
static bool s_authenticated;
static uint8_t wire_reply[17];
static int nacks;
static bool reader_mode;
static void esp_fill_random(void *out,size_t length){assert(length==16);memcpy(out,reader_mode ? reader_nonce : nonce,length);}
static void t2t_reset_auth(void){s_authenticated=false;t2t_aes_reset(&s_aes_session);}
static void tx_4bit_nack(uint8_t code){(void)code;nacks++;t2t_reset_auth();}
static void transmit(const uint8_t *data,uint16_t length,uint8_t bits){assert(length==17 && !bits);memcpy(wire_reply,data,length);}
/* ENGINE */
static t2t_aes_session_t peer;
static bool peer_secure, bad_crc;
static int fail_page = -1;
static bool fail_signature;
static int fail_counter = -1;
static bool write_fail;
static unsigned page_writes;
static bool iso14443a_check_crc(const uint8_t *data,size_t length) {
  (void)data;(void)length;return !bad_crc;
}
static int nfc_poller_transceive(const uint8_t *cmd,size_t length,bool crc,uint8_t *out,
    size_t capacity,size_t minimum,int timeout) {
  (void)crc;(void)capacity;(void)minimum;(void)timeout;
  if(cmd[0]==0x1A) { assert(length==2 && cmd[1]<=1);assert(t2t_aes_begin(&peer,key,nonce,out)==ESP_OK);return 19; }
  if(cmd[0]==0xAF) { assert(t2t_aes_finish(&peer,cmd,length,peer_secure,out)==ESP_OK);return 19; }
  if(cmd[0]==0xA2) {
    assert(length==(peer_secure?14:6));
    if(peer_secure){size_t n;assert(t2t_aes_verify(&peer,cmd,length,&n)==0 && n==6);}
    page_writes++;
    if(write_fail)return 0;
    if(peer_secure){assert(t2t_aes_sign(&peer,NULL,0,out)==0);return 10;}
    out[0]=0x0A;return 1;
  }
  assert(cmd[0]==0x30 || cmd[0]==0x3C || cmd[0]==0x39);
  if((cmd[0]==0x30 && cmd[1]==fail_page) || (cmd[0]==0x3C && fail_signature) ||
     (cmd[0]==0x39 && cmd[1]==fail_counter))return 0;
  if(peer_secure) { size_t n;assert(t2t_aes_verify(&peer,cmd,length,&n)==ESP_OK && n==2); }
  else assert(length==2);
  size_t n = cmd[0]==0x30 ? 16 : cmd[0]==0x3C ? 48 : 3;
  for(size_t i=0;i<n;i++)out[i]=(uint8_t)(cmd[1]*4+i);
  if(peer_secure)assert(t2t_aes_sign(&peer,out,n,out+n)==ESP_OK);
  return n + (peer_secure ? 8 : 0) + 2;
}
/* POLLER */
typedef int hb_nfc_err_t;
#define HB_NFC_ERR_PARAM ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_STATE -4
#define HEX_DIGITS_PER_BYTE 2U
#define HEX_NIBBLE_BITS 4U
#define HEX_ALPHA_VALUE 10U
#define portENTER_CRITICAL(m) ((void)0)
#define portEXIT_CRITICAL(m) ((void)0)
static struct { bool running; } s_status;
static uint8_t s_ul_aes_key[T2T_AES_BLOCK_SIZE];
static bool s_ul_aes_secure,s_has_ul_aes_key;
/* KEY PARSER */
int main(void) {
  t2t_aes_session_t session={0};uint8_t out[17],tag[8],data[16];size_t length;
  assert(nfc_protocol_service_ul_aes_key("1:000102030405060708090A0B0C0D0E0F")==ESP_OK);
  assert(s_has_ul_aes_key && s_ul_aes_secure && s_ul_aes_key[15]==15);
  assert(nfc_protocol_service_ul_aes_key("0:000102030405060708090A0B0C0D0E0Z")!=ESP_OK);
  assert(s_ul_aes_key[15]==15 && s_ul_aes_secure);
  s_status.running=true;assert(nfc_protocol_service_ul_aes_key(NULL)==ESP_ERR_INVALID_STATE);
  s_status.running=false;assert(nfc_protocol_service_ul_aes_key(NULL)==ESP_OK && !s_has_ul_aes_key);
  for(size_t i=0;i<16;i++)assert(s_ul_aes_key[i]==0);
  uint8_t memory[240];reader_mode=true;
  uint8_t version[]={0,4,3,1,4,0,15,3};assert(mful_is_aes(version));
  version[3]=2;assert(mful_is_aes(version));version[2]=4;assert(!mful_is_aes(version));
  for(unsigned secure=0;secure<2;secure++) {
    peer_secure=secure;
    assert(mful_aes_auth(&session,key,0,secure)==ESP_OK);
    assert(mful_aes_read_all(&session,memory,sizeof(memory))==60 && !session.is_authenticated);
    for(size_t i=0;i<sizeof(memory);i++)assert(memory[i]==(uint8_t)i);
    assert(mful_aes_auth(&session,key,1,secure)==ESP_OK);
    fail_page=8;assert(mful_aes_read_all(&session,memory,sizeof(memory))==8 && !session.is_authenticated);fail_page=-1;
    bad_crc=true;assert(mful_aes_auth(&session,key,0,secure)!=ESP_OK && !session.is_authenticated);bad_crc=false;
  }
  assert(mful_aes_auth(&session,key,2,true)==ESP_ERR_INVALID_ARG);
  nfc_t2t_file_t capture;version[2]=3;
  for(unsigned secure=0;secure<2;secure++) {
    peer_secure=secure;
    assert(mful_aes_auth(&session,key,0,secure)==ESP_OK);
    assert(mful_aes_read_card(&session,version,&capture)==ESP_OK && capture.length==240);
    assert(capture.config.has_aes_signature && capture.config.aes_signature[47]==47);
    assert(capture.config.counter_known==7 && capture.config.counters[2]==0x0A0908);
    assert(capture.config.uid[3]==4 && capture.config.uid[6]==7);
    assert(capture.config.aes_key_known[0]==UINT16_MAX && !capture.config.aes_key_known[1]);
    for(unsigned i=0;i<16;i++)assert(capture.memory[192+i]==key[15-i]);
    assert(!session.is_authenticated);
    fail_signature=true;
    assert(mful_aes_auth(&session,key,0,secure)==ESP_OK);
    assert(mful_aes_read_card(&session,version,&capture)==ESP_OK && !capture.config.has_aes_signature);
    assert(capture.config.counter_known==0);fail_signature=false;
    fail_counter=1;
    assert(mful_aes_auth(&session,key,0,secure)==ESP_OK);
    assert(mful_aes_read_card(&session,version,&capture)==ESP_OK && capture.config.counter_known==1);
    fail_counter=-1;fail_page=8;
    assert(mful_aes_auth(&session,key,0,secure)==ESP_OK);
    assert(mful_aes_read_card(&session,version,&capture)!=ESP_OK && capture.length==0);
    fail_page=-1;
  }
  for(unsigned secure=0;secure<2;secure++) {
    peer_secure=secure;
    assert(mful_aes_auth(&session,key,0,secure)==0);
    uint8_t page_data[4]={1,2,3,4};
    assert(mful_aes_write_page(&session,4,page_data)==0);
    assert(mful_aes_write_page(&session,48,page_data)==0);
    unsigned before=page_writes;
    assert(mful_aes_write_page(&session,0,page_data)!=0 && page_writes==before);
    assert(mful_aes_write_page(&session,41,page_data)!=0 && page_writes==before);
    write_fail=true;
    assert(mful_aes_write_page(&session,5,page_data)!=0 && !session.is_authenticated && page_writes==before+1);
    write_fail=false;
    assert(mful_aes_auth(&session,key,1,secure)==0);
    assert(mful_aes_write_page(&session,4,page_data)!=0);
  }
  reader_mode=false;
  uint8_t reader_frame[T2T_AES_AUTH_CONTINUE_SIZE];
  assert(t2t_aes_reader_challenge(&session,key,reader_nonce,challenge,sizeof(challenge),true,reader_frame)==ESP_OK);
  assert(!memcmp(reader_frame,continuation,sizeof(reader_frame)));
  assert(t2t_aes_reader_complete(&session,reply,sizeof(reply))==ESP_OK);
  assert(session.is_authenticated && session.is_secure && !memcmp(session.mac_key,session_key,16));
  assert(t2t_aes_sign(&session,command,2,tag)==ESP_OK && !memcmp(tag,command+2,8));
  uint8_t response[24];for(size_t i=0;i<16;i++)response[i]=i;
  memcpy(response+16,response_mac,8);
  assert(t2t_aes_verify(&session,response,sizeof(response),&length)==ESP_OK && length==16);
  for(size_t n=0;n<sizeof(reply);n++) {
    assert(t2t_aes_reader_challenge(&session,key,reader_nonce,challenge,sizeof(challenge),true,reader_frame)==ESP_OK);
    assert(t2t_aes_reader_complete(&session,reply,n)!=ESP_OK && !session.is_authenticated);
    assert(t2t_aes_reader_challenge(&session,key,reader_nonce,challenge,n,true,reader_frame)!=ESP_OK);
  }
  uint8_t bad_reply[17];memcpy(bad_reply,reply,17);bad_reply[8]^=1;
  assert(t2t_aes_reader_challenge(&session,key,reader_nonce,challenge,sizeof(challenge),true,reader_frame)==ESP_OK);
  assert(t2t_aes_reader_complete(&session,bad_reply,17)!=ESP_OK && !session.is_authenticated);
  for(size_t i=0;i<sizeof(data);i++)data[i]=i;
  assert(t2t_aes_begin(&session,key,nonce,out)==ESP_OK);
  assert(!memcmp(out,challenge,sizeof(out)) && session.is_pending);
  assert(t2t_aes_finish(&session,continuation,sizeof(continuation),true,out)==ESP_OK);
  assert(!memcmp(out,reply,sizeof(out)) && session.is_authenticated && session.is_secure);
  assert(!memcmp(session.mac_key,session_key,sizeof(session_key)));
  assert(t2t_aes_verify(&session,command,sizeof(command),&length)==ESP_OK && length==2);
  assert(t2t_aes_sign(&session,data,sizeof(data),tag)==ESP_OK && !memcmp(tag,response_mac,8));
  assert(t2t_aes_verify(&session,write_command,sizeof(write_command),&length)==ESP_OK && length==6);
  assert(t2t_aes_sign(&session,NULL,0,tag)==ESP_OK && !memcmp(tag,ack_mac,8));
  assert(t2t_aes_verify(&session,command,sizeof(command),&length)!=ESP_OK && length==0);
  assert(!session.is_authenticated && !session.is_secure);
  assert(t2t_aes_finish(&session,continuation,sizeof(continuation),true,out)!=ESP_OK);
  for(size_t n=0;n<sizeof(continuation);n++) {
    assert(t2t_aes_begin(&session,key,nonce,out)==ESP_OK);
    assert(t2t_aes_finish(&session,continuation,n,true,out)!=ESP_OK && !session.is_pending);
  }
  uint8_t wrong[sizeof(continuation)];memcpy(wrong,continuation,sizeof(wrong));wrong[20]^=1;
  assert(t2t_aes_begin(&session,key,nonce,out)==ESP_OK);
  assert(t2t_aes_finish(&session,wrong,sizeof(wrong),true,out)!=ESP_OK && !session.is_authenticated);
  assert(t2t_aes_begin(&session,key,nonce,out)==ESP_OK);
  assert(t2t_aes_finish(&session,continuation,sizeof(continuation),false,out)==ESP_OK);
  assert(session.is_authenticated && !session.is_secure);
  fail_crypto=true;
  assert(t2t_aes_begin(&session,key,nonce,out)!=ESP_OK && !session.is_pending);
  fail_crypto=false;
  assert(t2t_aes_begin(&session,key,nonce,out)==ESP_OK);
  assert(t2t_aes_finish(&session,continuation,sizeof(continuation),true,out)==ESP_OK);
  session.counter=UINT16_MAX-1;
  assert(t2t_aes_verify(&session,last_command,sizeof(last_command),&length)==ESP_OK);
  assert(t2t_aes_sign(&session,NULL,0,tag)==ESP_OK && !memcmp(tag,last_mac,8));
  assert(session.counter==UINT16_MAX);

  assert(t2t_aes_verify(&session,command,sizeof(command),&length)!=ESP_OK && !session.is_authenticated);
  const uint8_t first[]={0x1A,0};
  handle_aes_auth(first,sizeof(first));assert(nacks==1 && !s_aes_session.is_pending);
  s_aes_key_known[0]=UINT16_MAX;s_aes_key_known[1]=UINT16_MAX;
  s_mem[41*4]=2;
  handle_aes_auth(first,sizeof(first));assert(s_aes_session.is_pending);
  assert(!memcmp(wire_reply,challenge,17));
  handle_aes_auth(continuation,sizeof(continuation));
  assert(s_authenticated && s_aes_session.is_secure && !memcmp(wire_reply,reply,17));
  const uint8_t trace[]={0x1A,1};
  handle_aes_auth(trace,sizeof(trace));assert(!s_authenticated);
  handle_aes_auth(continuation,sizeof(continuation));
  assert(!s_authenticated && s_aes_session.is_authenticated);
  s_mem[42*4+2]=2;s_failed_auth_attempts=2;
  handle_aes_auth(first,sizeof(first));assert(!s_aes_session.is_pending);
  s_failed_auth_attempts=1;
  handle_aes_auth(first,sizeof(first));handle_aes_auth(wrong,sizeof(wrong));
  assert(s_failed_auth_attempts==2 && !s_authenticated);
  puts("Ultralight AES: NXP auth vector, independent CMAC, replay, malformed and failure tests passed");
}
