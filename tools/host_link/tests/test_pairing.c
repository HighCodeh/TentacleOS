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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sodium.h>
#include "host_link_sec.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
void pairing_test_boot(void);
void pairing_test_clock(int64_t);
void pairing_test_fail_mac(int);
void pairing_test_fail_persist(int);
static const uint8_t s_private[32]={0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a};
static const uint8_t s_public[32]={0x85,0x20,0xf0,0x09,0x89,0x30,0xa7,0x54,0x74,0x8b,0x7d,0xdc,0xb4,0x3e,0xf7,0x5a,0x0d,0xbf,0x3a,0x0d,0x26,0x38,0x1a,0xf4,0xeb,0xa4,0xa9,0x8e,0xaa,0x9b,0x4e,0x6a};
static uint8_t s_psk[32];
static void proof(const char *label,const uint8_t *data,size_t size,uint8_t *out) {
 uint8_t input[200],full[32];size_t prefix=strlen(label);memcpy(input,label,prefix);memcpy(input+prefix,data,size);
 assert(mbedtls_md_hmac(mbedtls_md_info_from_type(1),s_psk,sizeof(s_psk),input,prefix+size,full)==0);memcpy(out,full,16);
}
static void make_hello(uint8_t *out) {out[0]=2;memset(out+1,3,16);memcpy(out+17,s_public,32);proof("tos-host-v2-client",out,49,out+49);}
static void key(const char *label,const uint8_t *secret,const uint8_t *transcript,uint8_t *out) {
 uint8_t info[160];size_t n=strlen(label);memcpy(info,label,n);memcpy(info+n,transcript,104);
 assert(mbedtls_hkdf(mbedtls_md_info_from_type(1),s_psk,32,secret,32,info,n+104,out,32)==0);
}
static void sign(const uint8_t *key_bytes,const uint8_t *span,size_t length,uint8_t *mac) {
 uint8_t full[32];assert(mbedtls_md_hmac(mbedtls_md_info_from_type(1),key_bytes,32,span,length,full)==0);memcpy(mac,full,16);
}
int main(void) {
 pairing_test_boot();for(unsigned i=0;i<32;++i)s_psk[i]=(uint8_t)(i+1);
 uint8_t public_key[32];assert(crypto_scalarmult_curve25519_base(public_key,s_private)==0 && memcmp(public_key,s_public,32)==0);
 const uint8_t bob[32]={0xde,0x9e,0xdb,0x7d,0x7b,0x7d,0xc1,0xb4,0xd3,0x5b,0x61,0xc2,0xec,0xe4,0x35,0x37,0x3f,0x83,0x43,0xc8,0x5b,0x78,0x67,0x4d,0xad,0xfc,0x7e,0x14,0x6f,0x88,0x2b,0x4f}, shared[32]={0x4a,0x5d,0x9d,0x5b,0xa4,0xce,0x2d,0xe1,0x72,0x8e,0x3b,0xf4,0x80,0x35,0x0f,0x25,0xe0,0x7e,0x21,0xc9,0x47,0xd1,0x9e,0x33,0x76,0xf0,0x9b,0x3c,0x1e,0x16,0x17,0x42};
 uint8_t result[32];assert(crypto_scalarmult_curve25519(result,s_private,bob)==0 && memcmp(result,shared,32)==0);
 uint8_t hello[65],ack[71],old_ack[71];size_t length=99;make_hello(hello);
 assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_ERR_INVALID_STATE && length==0);
 host_link_sec_pairing_window(true);
 assert(host_link_sec_handle_hello(hello,17,ack,sizeof(ack),&length)==ESP_ERR_INVALID_ARG);
 hello[49]^=1;assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_ERR_INVALID_CRC);hello[49]^=1;
 memset(hello+17,0,32);proof("tos-host-v2-client",hello,49,hello+49);
 assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)!=ESP_OK && !host_link_sec_is_authenticated());
 make_hello(hello);pairing_test_fail_mac(ESP_FAIL);assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)!=ESP_OK);pairing_test_fail_mac(0);
 assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_OK && length==71);
 host_link_pairing_t pairing;assert(host_link_sec_pairing_get(&pairing));uint32_t stale=pairing.token;
 memcpy(old_ack,ack,sizeof(ack));assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_OK && memcmp(old_ack,ack,71)!=0);
 assert(host_link_sec_pairing_confirm(stale,true)==ESP_ERR_INVALID_STATE);assert(host_link_sec_pairing_get(&pairing));
 uint8_t secret[32],transcript[104],a2d[32],d2a[32],expected[16],mac[16];
 assert(crypto_scalarmult_curve25519(secret,s_private,ack+17)==0);memcpy(transcript,hello,49);memcpy(transcript+49,ack,55);
 proof("tos-host-v2-server",transcript,sizeof(transcript),expected);assert(memcmp(expected,ack+55,16)==0);
 key("tos-host-v2-a2d",secret,transcript,a2d);key("tos-host-v2-d2a",secret,transcript,d2a);
 uint8_t finish[]={1,1,1,0,0,0,3,0,0x12,0,0};sign(a2d,finish,sizeof(finish),mac);
 assert(!host_link_sec_finish(finish,sizeof(finish),mac,1) && !host_link_sec_is_authenticated());
 assert(!host_link_sec_sign_outbound(finish,sizeof(finish),expected));
 assert(host_link_sec_pairing_confirm(pairing.token,true)==ESP_OK);
 mac[0]^=1;assert(!host_link_sec_finish(finish,sizeof(finish),mac,1));mac[0]^=1;
 assert(!host_link_sec_finish(finish,sizeof(finish),mac,0));
 assert(host_link_sec_finish(finish,sizeof(finish),mac,1) && host_link_sec_is_authenticated());
 assert(host_link_sec_finish(finish,sizeof(finish),mac,1));
 assert(host_link_sec_sign_outbound(finish,sizeof(finish),expected));sign(d2a,finish,sizeof(finish),mac);assert(memcmp(mac,expected,16)==0);
 sign(a2d,finish,sizeof(finish),mac);assert(!host_link_sec_verify_inbound(finish,sizeof(finish),mac,1));
 uint8_t command[sizeof(finish)];memcpy(command,finish,sizeof(command));command[2]=2;command[8]=HOST_TYPE_CMD;
 sign(d2a,command,sizeof(command),mac);assert(!host_link_sec_verify_inbound(command,sizeof(command),mac,2));
 sign(a2d,command,sizeof(command),mac);assert(host_link_sec_verify_inbound(command,sizeof(command),mac,2));
 assert(!host_link_sec_verify_inbound(command,sizeof(command),mac,2));
 host_link_sec_pairing_window(true);host_link_sec_pairing_refresh();assert(host_link_sec_is_authenticated());
 host_link_sec_pairing_window(false);assert(host_link_sec_is_authenticated());
 host_link_sec_reset();host_link_sec_pairing_window(true);assert(!host_link_sec_is_authenticated() && !host_link_sec_sign_outbound(finish,sizeof(finish),expected));
 assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_OK);assert(host_link_sec_pairing_get(&pairing));
 assert(host_link_sec_pairing_confirm(pairing.token,false)==ESP_OK && !host_link_sec_pairing_get(&pairing));
 assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_OK);assert(host_link_sec_pairing_get(&pairing));
 pairing_test_clock(30000002);assert(host_link_sec_pairing_confirm(pairing.token,true)==ESP_ERR_INVALID_STATE);
 pairing_test_clock(60000002);assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_ERR_INVALID_STATE);
 host_link_sec_pairing_window(true);assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_OK);
 host_link_sec_pairing_window(false);assert(!host_link_sec_pairing_get(&pairing) && !pairing.can_pair);
 host_link_sec_pairing_refresh();assert(!host_link_sec_pairing_get(&pairing) && !pairing.can_pair);
 host_link_sec_pairing_window(true);
 pairing_test_clock(110000002);host_link_sec_pairing_refresh();
 pairing_test_clock(160000002);host_link_sec_pairing_refresh();
 assert(host_link_sec_handle_hello(hello,65,ack,sizeof(ack),&length)==ESP_OK);
 assert(host_link_sec_pairing_get(&pairing));uint32_t refreshed_token=pairing.token;
 host_link_sec_pairing_refresh();assert(host_link_sec_pairing_get(&pairing) && pairing.token==refreshed_token);
 pairing_test_clock(190000003);host_link_sec_pairing_refresh();
 assert(!host_link_sec_pairing_get(&pairing) && pairing.can_pair);
 assert(host_link_sec_pairing_confirm(refreshed_token,true)==ESP_ERR_INVALID_STATE);
 host_link_sec_pairing_window(false);host_link_sec_pairing_refresh();
 assert(!host_link_sec_pairing_get(&pairing) && !pairing.can_pair);
 pairing_test_fail_persist(ESP_FAIL);assert(host_link_sec_regenerate_psk()!=ESP_OK && !host_link_sec_is_authenticated());
 puts("PASS: real X25519/HMAC/HKDF, RFC 7748 public vector, PSK/transcript proofs, physical approval, replay and expiry");
 return 0;
}
