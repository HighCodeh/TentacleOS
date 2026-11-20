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
#include "rf_tag_test.h"
#include "lf_rfid.h"
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define HIGHBOY_NFC_CONFIG_DEFAULT() ((highboy_nfc_config_t){0})
#define RF_FRONTEND_NFC_13MHZ 2
#define RF_FRONTEND_LF_125KHZ 1
#define NFC_MANAGER_TECH_A 0
#define NFC_MANAGER_TECH_B 1
#define NFC_MANAGER_TECH_F 2
#define NFC_MANAGER_TECH_V 3
#define NFC_MANAGER_TECH_COUNT 4
typedef int rf_frontend_mode_t;
typedef struct{int unused;}highboy_nfc_config_t;
typedef struct{uint8_t uid[10],uid_len,atqa[2],sak;}card_a_t;
typedef struct{uint8_t pupi[4];}card_b_t;
typedef struct{uint8_t idm[8];}card_f_t;
typedef struct{uint8_t uid[8];}card_v_t;
typedef struct{uint8_t protocol;card_a_t iso14443a;struct{card_b_t iso14443b;card_f_t felica;card_v_t iso15693;}data;}hb_nfc_card_data_t;
typedef struct{int error;}rf_passive_status_t;
static int64_t now;
static bool boot=true,create_ok=true,reserved,field_on,nfc_initialized,cancel_on_read;
static int init_error,start_error,read_error=ESP_ERR_NOT_FOUND,off_failures,release_failures,reads,leds;
static unsigned found_tech;
static void(*queued)(void*);
static int held_seen;
static void vTaskDelay(unsigned ms);
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelete(void*p){assert(p==NULL&&!reserved&&!field_on);}
static const char*esp_err_to_name(int e){(void)e;return "error";}
static void led_signal_info(void){leds++;}
static void led_signal_error(void){leds++;}
static void led_signal_warning(void){leds++;}
static bool rf_passive_is_boot(void){return boot;}
static void rf_passive_snapshot(rf_passive_status_t*s){s->error=0;}
static int xTaskCreatePinnedToCore(void(*fn)(void*),const char*n,unsigned stack,void*arg,int priority,void*out,int core){assert(n&&stack>=6144&&!arg&&priority==4&&!out&&core==0);if(!create_ok)return 0;queued=fn;return 1;}
static int rf_frontend_reserve(int mode){assert(mode==1||mode==2);if(reserved)return ESP_ERR_INVALID_STATE;reserved=true;return 0;}
static int rf_frontend_unreserve(int mode){assert(mode&&reserved&&!field_on);if(release_failures){release_failures--;return ESP_FAIL;}reserved=false;return 0;}
static int highboy_nfc_init(const highboy_nfc_config_t*c){assert(c&&reserved);if(init_error)return init_error;nfc_initialized=true;return 0;}
static bool highboy_nfc_is_initialized(void){return nfc_initialized;}
static int off(void){if(off_failures){off_failures--;return ESP_FAIL;}field_on=false;return 0;}
static int highboy_nfc_field_off(void){return off();}
int lf_rfid_init(void){assert(reserved);return init_error;}
int lf_rfid_field_start(uint32_t hz,uint16_t duty){assert(reserved&&hz==125000&&duty==500);if(start_error)return start_error;field_on=true;return 0;}
int lf_rfid_field_stop_checked(void){return off();}
int lf_rfid_measure(uint32_t ms,lf_rfid_field_report_t*r){assert(field_on&&ms==100);r->measured_hz=124980;return 0;}
static int nfc_manager_poll(unsigned tech,hb_nfc_card_data_t*c){assert(nfc_initialized&&reserved);field_on=true;reads++;now+=100000;if(cancel_on_read)rf_tag_test_stop();if(read_error)return read_error;if(tech!=found_tech)return ESP_ERR_NOT_FOUND;c->protocol=7;c->iso14443a=(card_a_t){.uid={1,2,3,4},.uid_len=4,.atqa={4,0},.sak=8};memset(c->data.iso14443b.pupi,0x22,4);memset(c->data.felica.idm,0x33,8);memset(c->data.iso15693.uid,0x44,8);return 0;}
int lf_rfid_read_em4100(uint32_t ms,lf_rfid_em4100_t*t){assert(field_on&&reserved&&ms==500);reads++;now+=ms*1000;if(cancel_on_read)rf_tag_test_stop();if(!read_error){const uint8_t id[]={1,2,3,4,5};memcpy(t->bytes,id,5);}return read_error;}
void lf_rfid_read_stats(lf_rfid_read_stats_t*s){*s=(lf_rfid_read_stats_t){.edges=read_error?0:1000};}
/* SERVICE */
static void vTaskDelay(unsigned ms){now+=ms*1000;if(s_status.phase==RF_TAG_TEST_HELD){assert(s_status.busy&&reserved);held_seen++;assert(rf_tag_test_start(RF_TAG_TEST_LF)==ESP_ERR_INVALID_STATE);}}
static void reset(void){assert(!reserved&&!field_on);now=0;boot=create_ok=true;nfc_initialized=cancel_on_read=false;init_error=start_error=off_failures=release_failures=reads=held_seen=0;read_error=ESP_ERR_NOT_FOUND;queued=NULL;s_status=(rf_tag_test_status_t){0};}
static void run(unsigned kind){assert(rf_tag_test_start(kind)==0&&rf_tag_test_busy());assert(rf_tag_test_start(kind)==ESP_ERR_INVALID_STATE);assert(queued);queued(NULL);assert(!rf_tag_test_busy()&&!reserved&&!field_on);}
int main(void){
 reset();boot=false;assert(rf_tag_test_start(RF_TAG_TEST_NFC)==ESP_ERR_INVALID_STATE);boot=true;
 assert(rf_tag_test_start((rf_tag_test_kind_t)-1)==ESP_ERR_INVALID_ARG);
 create_ok=false;assert(rf_tag_test_start(RF_TAG_TEST_LF)==ESP_ERR_NO_MEM&&!rf_tag_test_busy());
 reset();run(RF_TAG_TEST_LF);assert(s_status.phase==RF_TAG_TEST_TIMEOUT&&s_status.cl_hz==124980&&s_status.da_edges==0&&now<32000000);
 reset();read_error=0;run(RF_TAG_TEST_LF);assert(s_status.phase==RF_TAG_TEST_FOUND&&s_status.uid_len==5&&s_status.uid[4]==5);
 for(found_tech=0;found_tech<4;found_tech++){reset();read_error=0;run(RF_TAG_TEST_NFC);assert(s_status.phase==RF_TAG_TEST_FOUND&&s_status.tech==found_tech&&s_status.uid_len==(found_tech<2?4:8));}
 found_tech=0;
 for(unsigned kind=0;kind<2;kind++){
  reset();init_error=ESP_FAIL;run(kind);assert(s_status.phase==RF_TAG_TEST_ERROR);
  reset();read_error=ESP_FAIL;run(kind);assert(s_status.phase==RF_TAG_TEST_ERROR);
  reset();cancel_on_read=true;run(kind);assert(s_status.phase==RF_TAG_TEST_CANCELLED&&reads==1);
  reset();assert(rf_tag_test_start(kind)==0);rf_tag_test_stop();queued(NULL);assert(s_status.phase==RF_TAG_TEST_CANCELLED&&!reads&&!reserved);
  reset();read_error=0;off_failures=2;release_failures=1;run(kind);assert(held_seen==3&&s_status.phase==RF_TAG_TEST_FOUND);
 }
 reset();start_error=ESP_FAIL;run(RF_TAG_TEST_LF);assert(s_status.phase==RF_TAG_TEST_ERROR);
 puts("PASS: active NFC A/B/F/V and LF, exclusive start, UID, timeout, before/during cancellation, init/read/field failures, held OFF and retry");
}
