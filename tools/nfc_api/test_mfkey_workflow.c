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
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define ESP_OK 0
#define HB_NFC_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NO_MEM -4
#define MFC_EMU_MAX_SECTORS 40
#define MF_KEY_A 0x60
#define MF_KEY_B 0x61
#define EXT_RAM_BSS_ATTR
#define ESP_LOGI(...) ((void)0)
#define TOS_PATH_NFC "."
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t,portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) assert(++*(p)==1)
#define portEXIT_CRITICAL(p) assert(--*(p)==0)
typedef struct {uint8_t uid[10],uid_len,atqa[2],sak;} nfc_iso14443a_data_t;
typedef struct {uint32_t cuid,nt,nr,ar;uint8_t sector;int key_type;} mfc_emu_capture_t;
typedef void (*mfc_emu_capture_cb_t)(const mfc_emu_capture_t *,void *);
typedef struct {const uint8_t *uid;int uid_len,sector,sector_count,type;const uint8_t *key;} mf_key_cache_store_params_t;
/* HEADERS */
static bool active,fail_start,fail_save,cancel_capture,cancel_recovery,fail_memory;
static unsigned power,stops,recoveries,persisted,random_value,progress_count;
static bool stopped_before_recovery;
static void vTaskDelay(int ticks) {(void)ticks;}
static unsigned esp_random(void) {return ++random_value;}
static esp_err_t storage_mkdir_recursive(const char *path) {mkdir(path,0700);return ESP_OK;}
static esp_err_t power_manager_no_sleep_acquire(void) {power++;return ESP_OK;}
static esp_err_t power_manager_no_sleep_release(void) {assert(power);power--;return ESP_OK;}
static bool nfc_listener_is_active(void) {return active;}
static bool nfc_listener_is_running(void) {return active;}
static void nfc_listener_stop(void) {stops++;active=false;}
static int nfc_listener_start_capture(const nfc_iso14443a_data_t *id,mfc_emu_capture_cb_t cb,void *ctx) {
 (void)id;if(fail_start)return ESP_FAIL;active=true;
 mfc_emu_capture_t a={.cuid=0x1234,.nt=0x1111,.nr=0x2222,.ar=0x3333,.sector=2,.key_type=MF_KEY_B};
 cb(&a,ctx);cb(&a,ctx); // duplicate must not count
 mfc_emu_capture_t wrong=a;wrong.cuid++;wrong.nr++;cb(&wrong,ctx); // mixed UID rejected
 a.nt++;a.nr++;cb(&a,ctx);cb(&a,ctx); // complete pair, bounded slot
 wrong.sector=40;cb(&wrong,ctx);wrong.sector=0;wrong.key_type=0;cb(&wrong,ctx);
 return ESP_OK;
}
static void mf_key_dict_init(void) {}
static void mf_key_cache_init(void) {}
static void mf_key_cache_save(void) {}
static int mf_classic_get_type(uint8_t sak) {(void)sak;return 1;}
static int mf_classic_get_sector_count(int type) {(void)type;return 16;}
static int mf_key_dict_add(const uint8_t *key) {assert(key[0]==0xa0 && key[5]==0xa5);if(fail_save)return ESP_FAIL;persisted++;return 0;}
static void mf_key_cache_store(const mf_key_cache_store_params_t *entry) {assert(entry->sector==2 && entry->type==MF_KEY_B);}
mfkey_result_t mfkey_recover(const mfkey_pair_t *pair,uint64_t *out,mfkey_progress_cb_t cb,void *ctx) {
 recoveries++;stopped_before_recovery=!active;
 assert(pair->uid==0x1234 && pair->nt0==0x1111 && pair->nt1==0x1112);
 assert(pair->nr0==0x2222 && pair->nr1==0x2223);
 if(!cb(0,ctx))return MFKEY_RESULT_CANCELLED;
 if(fail_memory)return MFKEY_RESULT_NO_MEMORY;
 *out=UINT64_C(0xa0a1a2a3a4a5);return MFKEY_RESULT_FOUND;
}
/* PRODUCTION */
static nfc_mfkey_control_t control(void *context) {
 (void)context;
 if(cancel_capture || (cancel_recovery && s_status.phase==NFC_MFKEY_RECOVER))return NFC_MFKEY_CANCEL;
 return NFC_MFKEY_FINISH_CAPTURE;
}
static void progress(const nfc_mfkey_status_t *status,void *context) {(void)context;assert(status);progress_count++;}
static void reset(void) {
 active=fail_start=fail_save=cancel_capture=cancel_recovery=fail_memory=false;
 power=stops=recoveries=persisted=progress_count=0;stopped_before_recovery=false;
}
int main(void) {
 const nfc_iso14443a_data_t id={.uid={1,2,3,4},.uid_len=4,.sak=8};
 const nfc_mfkey_callbacks_t callbacks={.control=control,.progress=progress};
 nfc_mfkey_status_t result;
 reset();assert(nfc_mfkey_run(&id,&callbacks,&result)==ESP_OK);
 assert(result.samples==2 && result.pairs==1 && result.processed==1 && result.recovered==1 && result.saved==1);
 assert(result.captures_saved && persisted==1 && stopped_before_recovery && stops==1 && power==0 && !active);
 reset();active=true;assert(nfc_mfkey_run(&id,&callbacks,&result)==ESP_ERR_INVALID_STATE);assert(active && stops==0 && power==0);
 reset();fail_start=true;assert(nfc_mfkey_run(&id,&callbacks,&result)==ESP_FAIL);assert(stops==0 && power==0 && !active);
 reset();cancel_capture=true;assert(nfc_mfkey_run(&id,&callbacks,&result)==ESP_OK);assert(result.cancelled && recoveries==0 && power==0 && !active);
 reset();cancel_recovery=true;assert(nfc_mfkey_run(&id,&callbacks,&result)==ESP_OK);assert(result.cancelled && result.recovered==0 && persisted==0 && power==0);
 reset();fail_memory=true;assert(nfc_mfkey_run(&id,&callbacks,&result)==ESP_ERR_NO_MEM);assert(result.recovery==MFKEY_RESULT_NO_MEMORY && power==0 && !active);
 reset();fail_save=true;assert(nfc_mfkey_run(&id,&callbacks,&result)==ESP_FAIL);assert(result.recovered==1 && result.saved==0 && power==0);
 puts("PASS: capture-to-dictionary workflow, duplicate/mixed UID rejection, RF stop before recovery, exclusive ownership, startup/memory/persistence failures and cancellation");
}
