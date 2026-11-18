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
#include <string.h>
#include <stdio.h>
#define ESP_OK 0
#define HB_NFC_OK 0
#define MFC_CMD_AUTH_KEY_A 0x60
#define MFC_CMD_AUTH_KEY_B 0x61
#define MF_KEY_A 0x60
#define MF_KEY_B 0x61
#define MFC_EMU_EVT_AUTH_SUCCESS 1
#define MFC_EMU_EVT_AUTH_FAIL 2
#define MF_PRNG_AR_ADVANCE 64
#define MF_PRNG_AT_ADVANCE 96
#define MFC_UID_DOUBLE_BYTES 7
#define ESP_LOGD(...) ((void)0)
typedef int hb_nfc_err_t, mf_key_type_t;
typedef struct {uint32_t odd,even;} crypto1_state_t;
typedef enum { MFC_EMU_STATE_ERROR, MFC_EMU_STATE_HALTED, MFC_EMU_STATE_AUTHENTICATED } mfc_emu_state_t;
typedef struct {uint32_t cuid,nt,nr,ar;uint8_t sector;mf_key_type_t key_type;} mfc_emu_capture_t;
static struct {
 struct {uint8_t uid[10];int uid_len,total_blocks;} card;
 struct {int total_auths,failed_auths,successful_auths;} stats;
 bool crypto_active,capture_pending;
 crypto1_state_t crypto;
 uint32_t auth_nt;int auth_sector;int auth_key_type;long last_activity_us;
 mfc_emu_capture_t capture;
 void (*capture_cb)(const mfc_emu_capture_t *,void *);
 void *capture_context;
} s_emu;
static struct {struct {int sector,key_type;} auth;} s_evt;
static bool bus_locked,tx_fail;
static unsigned deliveries,tx_count,nt_counter,key_queries;
static int rx_length=8;
static uint8_t response[]={0xce,0x99,0x85,0xf6,0x77,0x2f,0x55,0xbe};
static uint32_t emu_prng_next(void) {return ++nt_counter;}
static void reset_crypto_state(void) {s_emu.crypto_active=false;}
static int block_to_sector(int b) {return b<128?b/4:32+(b-128)/16;}
static int target_tx_raw(const uint8_t *p,size_t n,int bits) {
 assert(bus_locked && n==4 && bits==0);assert(p[3]==nt_counter);tx_count++;return tx_fail?-1:0;
}
static int target_rx_irq(uint8_t *p,size_t n,int timeout) {
 assert(bus_locked && n==8 && timeout==50);memcpy(p,response,8);return rx_length;
}
static int hb_nfc_spi_target_transaction_begin(void) {assert(!bus_locked);bus_locked=true;return 0;}
static int hb_nfc_spi_target_transaction_end(void) {assert(bus_locked);bus_locked=false;return 0;}
static void emit_event(int event) {(void)event;assert(!bus_locked);}
static bool get_key_for_sector(int sector,int type,uint64_t *key) {
 (void)sector;(void)type;(void)key;key_queries++;return false;
}
static void crypto1_init(crypto1_state_t *s,uint64_t key) {(void)s;(void)key;assert(false);}
static uint32_t crypto1_prng_successor(uint32_t nt,int n) {(void)nt;(void)n;assert(false);return 0;}
static bool auth_try_decode(const uint8_t *p,uint32_t ar,const crypto1_state_t *s,crypto1_state_t *v) {
 (void)p;(void)ar;(void)s;(void)v;assert(false);return false;
}
static int target_tx_nonce(uint32_t nt,bool nested) {(void)nt;(void)nested;assert(false);return -1;}
static int target_tx_encrypted(const uint8_t *p,size_t n) {(void)p;(void)n;assert(false);return -1;}
static long esp_timer_get_time(void) {return 0;}
static mfc_emu_state_t authenticate(uint8_t,uint8_t);
/* PRODUCTION */
static void receive(const mfc_emu_capture_t *sample,void *context) {
 (void)context;assert(!bus_locked);deliveries++;
 assert(sample->cuid==0x2a234f80 && sample->nt==nt_counter);
 assert(sample->nr==0xce9985f6 && sample->ar==0x772f55be);
 assert(sample->sector==39 && sample->key_type==MF_KEY_B);
}
int main(void) {
 s_emu.card.total_blocks=256;s_emu.card.uid_len=7;
 const uint8_t uid[]={4,1,2,0x2a,0x23,0x4f,0x80};memcpy(s_emu.card.uid,uid,7);
 s_emu.capture_cb=receive;
 assert(handle_auth(MFC_CMD_AUTH_KEY_B,255)==MFC_EMU_STATE_HALTED);
 assert(handle_auth(MFC_CMD_AUTH_KEY_B,255)==MFC_EMU_STATE_HALTED);
 assert(deliveries==2 && tx_count==2 && key_queries==0 && !s_emu.crypto_active);
 rx_length=7;assert(handle_auth(MFC_CMD_AUTH_KEY_B,255)==MFC_EMU_STATE_HALTED);assert(deliveries==2);
 rx_length=8;tx_fail=true;assert(handle_auth(MFC_CMD_AUTH_KEY_B,255)==MFC_EMU_STATE_ERROR);assert(deliveries==2);
 tx_fail=false;s_emu.crypto_active=true;unsigned count=tx_count;
 assert(handle_auth(MFC_CMD_AUTH_KEY_B,255)==MFC_EMU_STATE_HALTED && tx_count==count);
 s_emu.crypto_active=false;s_emu.card.total_blocks=64;
 assert(handle_auth(MFC_CMD_AUTH_KEY_B,255)==MFC_EMU_STATE_HALTED && tx_count==count);
 puts("PASS: unknown-key reader capture preserves on-air words, 7-byte CUID, fresh nonce, callback after bus release, malformed/RF failures and nested exclusion");
}
