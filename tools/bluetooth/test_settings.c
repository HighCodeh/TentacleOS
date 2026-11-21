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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "tos_config.h"
#define ESP_LOGE(tag, ...) ((void)(tag))
static const char *TAG = "CONFIG_TEST";
tos_config_wifi_t g_config_wifi;
tos_config_ble_t g_config_ble;
/* FUNCTIONS */
static unsigned allocations, fail_at;
static void *allocate(size_t size) { return ++allocations == fail_at ? NULL : malloc(size); }
static void check_oom(cJSON *(*serialize)(void)) {
 cJSON_Hooks hooks={.malloc_fn=allocate,.free_fn=free};cJSON_InitHooks(&hooks);
 fail_at=0;allocations=0;cJSON *root=serialize();assert(root);unsigned total=allocations;cJSON_Delete(root);
 for(fail_at=1;fail_at<=total;++fail_at) {allocations=0;root=serialize();assert(root==NULL);}
 cJSON_InitHooks(NULL);
}
int main(void) {
 cJSON *root=cJSON_Parse("{\"enabled\":true,\"ssid\":\"Legacy\"}");assert(root);parse_ble(root);cJSON_Delete(root);
 assert(!g_config_ble.enabled && !strcmp(g_config_ble.name,"Legacy") && g_config_ble.advertising && g_config_ble.max_conn==3);
 root=cJSON_Parse("{\"enabled\":true,\"enabled_by_user\":true,\"name\":\"My Highboy\",\"max_conn\":2,\"advertising\":false}");
 assert(root);parse_ble(root);cJSON_Delete(root);assert(g_config_ble.enabled && !g_config_ble.advertising && g_config_ble.max_conn==2);
 root=serialize_ble();memset(&g_config_ble,0,sizeof(g_config_ble));parse_ble(root);cJSON_Delete(root);
 assert(!strcmp(g_config_ble.name,"My Highboy") && g_config_ble.enabled && !g_config_ble.advertising && g_config_ble.max_conn==2);
 g_config_ble.enabled=false;root=serialize_ble();parse_ble(root);cJSON_Delete(root);assert(!g_config_ble.enabled);
 root=cJSON_Parse("{\"enabled\":true,\"ssid\":\"Factory\",\"password\":\"password\",\"max_conn\":4,\"ip_addr\":\"192.168.4.1\"}");
 parse_wifi(root);cJSON_Delete(root);assert(g_config_wifi.enabled && !strcmp(g_config_wifi.ap.ssid,"Factory") && g_config_wifi.ap.max_conn==4);
 strcpy(g_config_wifi.client.ssid,"external");strcpy(g_config_wifi.client.password,"external-password");
 root=serialize_wifi();memset(&g_config_wifi,0,sizeof(g_config_wifi));parse_wifi(root);cJSON_Delete(root);
 assert(g_config_wifi.enabled && !strcmp(g_config_wifi.ap.ssid,"Factory") && !strcmp(g_config_wifi.client.ssid,"external"));
 assert(!strcmp(g_config_wifi.client.password,"external-password") && !strcmp(g_config_wifi.ap.password,"password"));
 check_oom(serialize_ble);check_oom(serialize_wifi);
 puts("PASS: real cJSON radio preferences, legacy BLE-off migration, names/limits/visibility, Wi-Fi factory/nested formats and every serializer allocation failure");
}
