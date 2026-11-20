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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define T4T_EMU_ATS_CAPACITY 32
#define RF_FRONTEND_NFC_13MHZ 1
#define DESFIRE_RETRY_MS 10
#define pdMS_TO_TICKS(x) (x)
#define taskYIELD() ((void)0)
typedef int hb_nfc_err_t;
typedef int esp_err_t;
typedef struct {uint8_t uid[7],uid_len,atqa[2],sak,ats[32],ats_len;} t4t_emu_identity_t;
static uint8_t k_uid7[7],k_atqa[2],k_sak,k_ats[32],s_uid_len,s_ats_len;
static bool s_is_identity_only;
static bool s_has_desfire = true, s_reserved, s_initialized = true;
static struct {t4t_emu_identity_t identity;} s_desfire_card;
static unsigned s_failure, s_stops, s_steps, s_cancels;
static hb_nfc_err_t t4t_emu_init_default(void) {s_is_identity_only = false;return 0;}
/* IDENTITY */
static int rf_frontend_reserve(int kind) {(void)kind; if(s_failure==1)return -1; assert(!s_reserved);s_reserved=true;return 0;}
static int rf_frontend_unreserve(int kind) {(void)kind;assert(s_reserved);s_reserved=false;return 0;}
static int highboy_nfc_field_off(void) {return s_failure==2?-1:0;}
static int t4t_emu_configure_target(void) {return s_failure==3?-1:0;}
static int t4t_emu_start(void) {return s_failure==4?-1:0;}
static void t4t_emu_run_step(void) {s_steps++;}
static bool t4t_emu_has_error(void) {return s_failure==5;}
static void t4t_emu_stop(void) {s_stops++;}
static void highboy_nfc_deinit(void) {s_initialized=false;}
static bool highboy_nfc_is_initialized(void) {return s_initialized;}
static bool cancel_requested(void) {return ++s_cancels>2;}
static void vTaskDelay(int ticks) {(void)ticks;}
/* LIFECYCLE */
int main(void) {
  t4t_emu_identity_t identity = {.uid={1,2,3,4,5,6,7},.uid_len=7,.atqa={0x44,0},.sak=0x20,.ats={3,0x40,2},.ats_len=3};
  assert(t4t_emu_init_identity(&identity)==0 && s_is_identity_only);
  assert(!memcmp(k_uid7,identity.uid,7) && !memcmp(k_ats,identity.ats,3));
  assert(s_uid_len==7 && s_ats_len==3 && k_sak==0x20 && k_atqa[0]==0x44);
  identity.uid_len=4; identity.ats_len=1; identity.ats[0]=1;
  assert(t4t_emu_init_identity(&identity)==0 && s_uid_len==4 && !k_uid7[4]);
  identity.ats[0]=3; assert(t4t_emu_init_identity(&identity)!=0);
  identity.ats[0]=1; identity.uid_len=5; assert(t4t_emu_init_identity(&identity)!=0);
  identity.uid_len=4; s_desfire_card.identity=identity;
  for (s_failure=0;s_failure<=5;s_failure++) {
    s_stops=s_steps=s_cancels=0; s_initialized=true;
    int error=emulate_desfire();
    assert((error==0)==(s_failure==0));
    assert(!s_reserved && s_stops==(s_failure==1?0U:1U));
    if(s_failure!=1) assert(!s_initialized);
    if(!s_failure) assert(s_steps==2);
  }
  puts("DESFire identity configuration and emulation failure/cancel cleanup passed");
}
