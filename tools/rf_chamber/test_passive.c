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
typedef int esp_err_t, nvs_handle_t, StaticSemaphore_t;
typedef void *SemaphoreHandle_t;
typedef struct {esp_err_t c5, cc, sx, nfc, lf;} rf_lab_hw_status_t;
typedef struct {rf_lab_hw_status_t hardware; esp_err_t error; uint8_t path;} rf_passive_status_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_INVALID_RESPONSE 4
#define ESP_ERR_NVS_NOT_FOUND 5
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define portMAX_DELAY 999
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
static int stored, pending, open_error, commit_error, quiet_error, select_error, lock_ok=1;
static int commits, writes, quiets, no_sleep, selected=1;
static void *xSemaphoreCreateMutexStatic(void *p){return p;}
static int xSemaphoreTake(void *p,int ticks){assert(p);(void)ticks;return lock_ok;}
static void xSemaphoreGive(void *p){assert(p);}
static int nvs_open(const char *ns,int mode,int *h){assert(strcmp(ns,"rf_lab")==0);(void)mode;*h=1;return open_error;}
static int nvs_get_u8(int h,const char *key,uint8_t *v){assert(h==1 && !strcmp(key,"passive"));*v=stored;return 0;}
static int nvs_set_u8(int h,const char *key,uint8_t v){assert(h==1 && !strcmp(key,"passive"));pending=v;writes++;return 0;}
static int nvs_commit(int h){assert(h==1);commits++;if(!commit_error)stored=pending;return commit_error;}
static void nvs_close(int h){assert(h==1);}
static void rf_lab_hw_quiet(rf_lab_hw_status_t *s){memset(s,0,sizeof(*s));s->sx=quiet_error;quiets++;}
static int power_manager_no_sleep_acquire(void){no_sleep++;return 0;}
static int rf_lab_hw_path_set(uint8_t p){if(p<1||p>4)return ESP_ERR_INVALID_ARG;if(select_error)return select_error;selected=p;return 0;}
static int rf_lab_hw_path_get(uint8_t *p){*p=selected;return 0;}
static void led_signal_info(void){}
static void led_signal_error(void){}
/* PRODUCTION */
static void boot(int value){stored=value;s_status=(rf_passive_status_t){.path=1};s_is_boot=false;}
int main(void){
 boot(0);assert(rf_passive_load()==0 && !rf_passive_is_boot());rf_passive_startup();assert(quiets==0);
 assert(rf_passive_select(1)==ESP_ERR_INVALID_STATE);
 for(int p=1;p<=4;p++){
  boot(p);assert(rf_passive_load()==0 && rf_passive_is_boot());rf_passive_startup();
  rf_passive_status_t s={0};rf_passive_snapshot(&s);assert(s.error==0 && s.path==p && selected==p);
  for(int q=1;q<=4;q++){assert(rf_passive_select(q)==0 && stored==q && selected==q);}
 }
 assert(quiets==4 && no_sleep==4);
 int previous=writes;assert(rf_passive_select(0)==ESP_ERR_INVALID_ARG);assert(rf_passive_select(5)==ESP_ERR_INVALID_ARG);assert(writes==previous);
 lock_ok=0;assert(rf_passive_select(2)==ESP_ERR_TIMEOUT);lock_ok=1;
 commit_error=ESP_ERR_TIMEOUT;assert(rf_passive_select(3)==ESP_ERR_TIMEOUT && stored==4);commit_error=0;
 assert(rf_passive_save_boot(false,3)==0 && stored==0);boot(stored);assert(rf_passive_load()==0 && !rf_passive_is_boot());
 boot(255);assert(rf_passive_load()==ESP_ERR_INVALID_STATE && rf_passive_is_boot());rf_passive_startup();assert(s_status.error!=0);
 boot(2);open_error=ESP_ERR_TIMEOUT;assert(rf_passive_load()==ESP_ERR_TIMEOUT && rf_passive_is_boot());open_error=0;
 boot(2);quiet_error=ESP_ERR_TIMEOUT;assert(rf_passive_load()==0);rf_passive_startup();assert(s_status.error==ESP_ERR_TIMEOUT);quiet_error=0;
 boot(1);assert(rf_passive_load()==0);rf_passive_startup();selected=4;rf_passive_status_t s;rf_passive_snapshot(&s);assert(s.error==ESP_ERR_INVALID_RESPONSE);
 puts("PASS: passive boot, 4 states persisted, normal exit, invalid state, NVS failure, lock timeout, hardware failure and readback mismatch");
}
