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
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
static const char *TAG = "ANNOUNCE_TEST";
#define BLE_ADVERTISED_NAME_MAX 29
#define HID_NAME_MAX_BYTES 29
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_HS_ADV_TX_PWR_LVL_AUTO 127
#define BLE_SVC_DIS_UUID16 0x180A
#define UUID_HID_SVC 0x1812
#define HID_APPEARANCE_GENERIC 0x03C0
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_HS_FOREVER -1
#define BLE_UUID16_INIT(n) {.value=n}
typedef struct {uint16_t value;} ble_uuid16_t;
struct ble_gap_adv_params {int conn_mode,disc_mode;};
struct ble_hs_adv_fields {int flags,tx_pwr_lvl_is_present,tx_pwr_lvl,num_uuids16,uuids16_is_complete,name_is_complete,appearance;ble_uuid16_t *uuids16;uint8_t *name;size_t name_len;};
static unsigned starts;
static bool running,active,s_adv_requested,s_advertising_requested,s_is_connected;
static unsigned s_conn_count,s_max_conn,s_own_addr_type;
static char device_name[32],received[32];
static bool complete;
static int fail_fields,fail_response,fail_start;
static bool bluetooth_service_is_running(void) {return running;}
static bool ble_gap_adv_active(void) {return active;}
static int ble_gap_adv_stop(void) {active=false;return 0;}
static uint8_t bluetooth_service_get_own_addr_type(void) {return s_own_addr_type;}
static const char *ble_svc_gap_device_name(void) {return device_name;}
static int gap_event(void *event,void *arg) {return 0;}
static int hid_gap_event(void *event,void *arg) {return 0;}
static int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *fields) {
 assert(fields->name_len==0 && fields->flags==6 && fields->num_uuids16==1);return fail_fields;
}
static int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *fields) {
 assert(fields->name_len+2<=31);memcpy(received,fields->name,fields->name_len);received[fields->name_len]=0;
 complete=fields->name_is_complete;return fail_response;
}
static int ble_gap_adv_start(uint8_t type,const void *peer,int duration,const struct ble_gap_adv_params *params,int (*cb)(void *,void *),void *arg) {
 assert(params->conn_mode==1 && params->disc_mode==2);++starts;active=fail_start==0;return fail_start;
}
/* FUNCTIONS */
int main(void) {
 strcpy(device_name,"12345678901234567890123456789");
 assert(bluetooth_service_start_advertising()==ESP_ERR_INVALID_STATE);assert(!starts);
 running=true;
 for(s_max_conn=1;s_max_conn<=3;++s_max_conn) {
  s_conn_count=s_max_conn;active=false;unsigned before=starts;
  assert(bluetooth_service_start_advertising()==ESP_OK && starts==before && s_adv_requested);
  --s_conn_count;assert(bluetooth_service_start_advertising()==ESP_OK && starts==before+1);
  assert(complete && !strcmp(received,device_name));
 }
 active=false;s_conn_count=0;fail_response=1;assert(bluetooth_service_start_advertising()==ESP_FAIL && !s_adv_requested);
 fail_response=0;fail_start=1;assert(bluetooth_service_start_advertising()==ESP_FAIL && !s_adv_requested);fail_start=0;
 s_advertising_requested=true;assert(hid_start_advertising()==ESP_OK && complete && !strcmp(received,device_name));
 unsigned before=starts;s_is_connected=true;assert(hid_start_advertising()==ESP_OK && starts==before);
 s_is_connected=false;strcat(device_name,"AB");assert(hid_start_advertising()==ESP_OK && !complete && strlen(received)==29);
 puts("PASS: generic BLE connection limits, 29-byte name in scan response, advertisement failures and HID configured name/single-peer behavior");
}
