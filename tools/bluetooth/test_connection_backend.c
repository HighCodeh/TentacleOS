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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NO_MEM 5
#define ESP_ERR_INVALID_SIZE 6
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED 7
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED 8
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
static const char *TAG = "CONNECTION_BACKEND_TEST";
#define SSID_MAX_LEN 32
#define PASSWORD_MAX_LEN 64
#define IP_ADDR_MAX_LEN 16
#define AP_PASSWORD_MIN_BYTES 8
#define AP_MAX_CLIENTS 8
#define AP_CONFIG_LOCK_MS 1000
#define WIFI_AP_CONFIG_FILE "ap.conf"
#define ASSETS_WRITE_PATH_SIZE 160
#define ASSETS_MOUNT_POINT mount_point
static char mount_point[128];
static bool s_is_initialized, s_is_active, s_is_connected, s_promiscuous_active;
static int lock_taken;
static void *s_mutex = &lock_taken;
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
static int xSemaphoreTake(void *mutex, int ticks) { assert(mutex && ticks == 1000); if (lock_taken) return 0; lock_taken=1; return 1; }
static void xSemaphoreGive(void *mutex) { assert(mutex && lock_taken); lock_taken=0; }
typedef struct { struct { uint8_t ssid[32], password[64], ssid_len, max_connection, authmode; } ap; } wifi_config_t;
typedef struct { uint32_t addr; } ip4_t;
typedef struct { ip4_t ip, gw, netmask; } esp_netif_ip_info_t;
typedef int esp_netif_t;
#define WIFI_IF_AP 1
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
static wifi_config_t active_config;
static esp_netif_ip_info_t active_ip;
static int starts, stops, set_calls, fail_set, fail_start, fail_stop, fail_json;
static bool radio_started;
static int esp_wifi_get_config(int interface, wifi_config_t *out) { assert(interface==1); *out=active_config; return ESP_OK; }
static int esp_wifi_set_config(int interface, const wifi_config_t *config) { ++set_calls; if(fail_set) {fail_set=0;return ESP_FAIL;} active_config=*config; return ESP_OK; }
static int esp_wifi_stop(void) { ++stops; if(fail_stop) {fail_stop=0;return ESP_FAIL;} radio_started=false;return ESP_OK; }
static int esp_wifi_start(void) { ++starts; if(fail_start) {fail_start=0;return ESP_FAIL;} radio_started=true;return ESP_OK; }
static esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) { assert(!strcmp(key,"WIFI_AP_DEF")); static int netif;return &netif; }
static int esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *out) { *out=active_ip;return ESP_OK; }
static int esp_netif_set_ip_info(esp_netif_t *netif, const esp_netif_ip_info_t *info) { active_ip=*info;return ESP_OK; }
static int esp_netif_str_to_ip4(const char *text, ip4_t *out) { return inet_pton(AF_INET,text,&out->addr)==1 ? ESP_OK : ESP_FAIL; }
static int esp_netif_dhcps_stop(esp_netif_t *netif) {return ESP_OK;}
static int esp_netif_dhcps_start(esp_netif_t *netif) {return ESP_OK;}
typedef struct {int unused;} cJSON;
static cJSON *cJSON_CreateObject(void) {return fail_json ? NULL : calloc(1,sizeof(cJSON));}
static cJSON *cJSON_AddStringToObject(cJSON *root,const char *key,const char *value) {return root;}
static cJSON *cJSON_AddNumberToObject(cJSON *root,const char *key,int value) {return root;}
static cJSON *cJSON_AddBoolToObject(cJSON *root,const char *key,bool value) {return root;}
static char *cJSON_PrintUnformatted(cJSON *root) {return strdup("new-config");}
static void cJSON_Delete(cJSON *root) {free(root);}
static int io_failure;
static size_t write_file(const void *ptr,size_t size,size_t count,FILE *file) {return io_failure==1 ? 0 : fwrite(ptr,size,count,file);}
static int flush_file(FILE *file) {int error=fflush(file);return io_failure==2 ? -1 : error;}
static int close_file(FILE *file) {int error=fclose(file);return io_failure==3 ? -1 : error;}
static int rename_file(const char *old,const char *next) {return io_failure==4 ? -1 : rename(old,next);}
#define fwrite write_file
#define fflush flush_file
#define fclose close_file
#define rename rename_file
/* STORAGE */
#undef fwrite
#undef fflush
#undef fclose
#undef rename
/* WIFI */
static void read_saved(char *out) {
 char path[200];snprintf(path,sizeof(path),"%s/ap.conf",mount_point);FILE *file=fopen(path,"r");assert(file);
 size_t size=fread(out,1,99,file);out[size]=0;assert(!fclose(file));
 snprintf(path,sizeof(path),"%s/ap.conf.tmp",mount_point);assert(access(path,F_OK)!=0);
}
int main(void) {
 strcpy(mount_point,"/tmp/highboy-radio-XXXXXX");assert(mkdtemp(mount_point));s_is_initialized=true;
 assert(storage_assets_write_file("ap.conf","original")==ESP_OK);
 for(io_failure=1;io_failure<=4;++io_failure) {
  assert(storage_assets_write_file("ap.conf","new")!=ESP_OK);char saved[100];read_saved(saved);assert(!strcmp(saved,"original"));
 }
 io_failure=0;
 assert(wifi_service_save_ap_config("AP","short",3,"192.168.4.1",true)==ESP_ERR_INVALID_ARG);
 assert(wifi_service_save_ap_config("AP","password",9,"192.168.4.1",true)==ESP_ERR_INVALID_ARG);
 fail_json=1;assert(wifi_service_save_ap_config("AP","password",3,"192.168.4.1",true)==ESP_ERR_NO_MEM);
 assert(!starts && !stops && !set_calls && !lock_taken);fail_json=0;
 const char *full="12345678901234567890123456789012";
 assert(wifi_service_save_ap_config(full,"password",8,"192.168.4.1",true)==ESP_OK);
 assert(active_config.ap.ssid_len==32 && !memcmp(active_config.ap.ssid,full,32));
 assert(active_config.ap.max_connection==8 && active_config.ap.authmode==WIFI_AUTH_WPA2_PSK);
 assert(!strcmp((char *)active_config.ap.password,"password") && s_is_active && radio_started);
 wifi_config_t previous=active_config;esp_netif_ip_info_t previous_ip=active_ip;
 for(unsigned failure=0;failure<4;++failure) {
  if(failure==0)fail_set=1; if(failure==1)fail_start=1; if(failure==2)io_failure=2; if(failure==3)fail_stop=1;
  assert(wifi_service_save_ap_config("changed","",1,"192.168.5.1",true)!=ESP_OK);
  assert(!memcmp(&previous,&active_config,sizeof(previous)) && active_ip.ip.addr==previous_ip.ip.addr);
  assert(s_is_active && radio_started && !lock_taken);io_failure=0;
 }
 assert(wifi_service_save_ap_config("changed","",2,"192.168.5.1",false)==ESP_OK);
 assert(!s_is_active && !radio_started && active_config.ap.authmode==WIFI_AUTH_OPEN);
 assert(active_config.ap.password[0]==0 && active_config.ap.max_connection==2);
 s_promiscuous_active=true;int count=set_calls;
 assert(wifi_service_save_ap_config("AP","password",3,NULL,true)==ESP_ERR_INVALID_STATE);assert(count==set_calls);
 char path[200];snprintf(path,sizeof(path),"%s/ap.conf",mount_point);assert(!remove(path));assert(!rmdir(mount_point));
 puts("PASS: C5 live AP configuration, 32-byte SSID, auth/client limit/on-off, allocation/driver/write failures and rollback; failed writes preserve previous file");
}
