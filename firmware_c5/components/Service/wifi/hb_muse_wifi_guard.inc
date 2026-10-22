// SPDX-License-Identifier: GPL-3.0-or-later
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

/* Included only in isolated wifi_service.c; uses the existing service mutex.
 * The setup SPI owner gate excludes legacy Wi-Fi commands during provisioning.
 * Existing legacy connect behavior is unchanged. Account commit is separate.
 */
#include "hb_muse_wifi_json_bound.h"
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
static void muse_secret_wipe(void *p, size_t n) {
    volatile unsigned char *b=p; while(n--) *b++=0;
}
static void muse_json_clear(cJSON *root) {
    if(!root) return;
    if(root->valuestring) muse_secret_wipe(root->valuestring,strlen(root->valuestring));
    for(cJSON *child=root->child;child;child=child->next) muse_json_clear(child);
}
static void muse_json_wipe(cJSON *root) { muse_json_clear(root); cJSON_Delete(root); }
bool hb_muse_wifi_provision_guarded(const char *ssid,const char *password,
                                   bool (*guard)(void *),void *user) {
    if(!ssid || !password || !guard || !s_mutex || !s_is_active ||
       strnlen(ssid,33)==0 || strnlen(ssid,33)>32 || strnlen(password,65)>64 ||
       !storage_assets_is_mounted() || !guard(user)) return false;
    if(xSemaphoreTake(s_mutex,0)!=pdTRUE) return false;
    bool ok=false,temp_created=false; uint8_t *original=NULL,*check=NULL; char *encoded=NULL;
    size_t original_n=0,check_n=0; cJSON *root=NULL; wifi_config_t cfg={0};
    char path[256],temp_path[256];
    const char *mount=storage_assets_get_mount_point();
    if(!mount || snprintf(path,sizeof(path),"%s/%s",mount,WIFI_KNOWN_NETWORKS_FILE)>=(int)sizeof(path) ||
       snprintf(temp_path,sizeof(temp_path),"%s/%s.muse-tmp",mount,WIFI_KNOWN_NETWORKS_FILE)>=(int)sizeof(temp_path)) goto done;
    struct stat info;
    if(stat(temp_path,&info)==0 || errno!=ENOENT) goto done; /* preserve old recovery/staging data */
    if(stat(path,&info)==0) {
        if(!S_ISREG(info.st_mode) || info.st_size<2 || info.st_size>32768) goto done;
        original=storage_assets_load_file(WIFI_KNOWN_NETWORKS_FILE,&original_n);
        if(!original || original_n!=(size_t)info.st_size ||
           !hb_muse_wifi_json_shallow(original,original_n)) goto done;
        uint8_t *terminated=realloc(original,original_n+1u);
        if(!terminated) goto done;
        original=terminated; original[original_n]=0; /* loader returns exactly n bytes */
        root=cJSON_ParseWithLengthOpts((const char *)original,original_n+1,NULL,true);
        if(!root || !cJSON_IsArray(root)) goto done; /* no corrupt-data reset */
    } else {
        if(errno!=ENOENT) goto done;
        root=cJSON_CreateArray(); if(!root) goto done;
    }
    cJSON *match=NULL;
    for(cJSON *entry=root->child;entry;entry=entry->next) {
        cJSON *s=cJSON_GetObjectItemCaseSensitive(entry,"ssid");
        cJSON *p=cJSON_GetObjectItemCaseSensitive(entry,"password");
        if(!cJSON_IsObject(entry) || !cJSON_IsString(s) || !s->valuestring ||
           !cJSON_IsString(p) || !p->valuestring || strlen(s->valuestring)>32 || strlen(p->valuestring)>64) goto done;
        unsigned ssids=0,passwords=0;
        for(cJSON *item=entry->child;item;item=item->next) {
            if(!item->string) goto done;
            ssids+=strcmp(item->string,"ssid")==0; passwords+=strcmp(item->string,"password")==0;
        }
        if(ssids!=1 || passwords!=1) goto done;
        if(strcmp(s->valuestring,ssid)==0) { if(match) goto done; match=entry; }
    }
    if(match) {
        cJSON *p=cJSON_CreateString(password); if(!p) goto done;
        if(!cJSON_ReplaceItemInObjectCaseSensitive(match,"password",p)) { cJSON_Delete(p); goto done; }
    } else {
        cJSON *entry=cJSON_CreateObject();
        if(!entry) goto done;
        if(!cJSON_AddStringToObject(entry,"ssid",ssid) || !cJSON_AddStringToObject(entry,"password",password) ||
           !cJSON_AddItemToArray(root,entry)) { muse_json_wipe(entry); goto done; }
    }
    encoded=cJSON_PrintUnformatted(root);
    if(!encoded || strlen(encoded)>32768 || !guard(user)) goto done;
    temp_created=true;
    if(storage_assets_write_file(WIFI_KNOWN_NETWORKS_FILE ".muse-tmp",encoded)!=ESP_OK || !guard(user)) goto done;
    check=storage_assets_load_file(WIFI_KNOWN_NETWORKS_FILE ".muse-tmp",&check_n);
    if(!check || check_n!=strlen(encoded) || memcmp(check,encoded,check_n)!=0 || !guard(user)) goto done;
    if(rename(temp_path,path)!=0) goto done;
    temp_created=false;
    if(!guard(user)) goto done;
    muse_secret_wipe(check,check_n); free(check); check=NULL; check_n=0;
    check=storage_assets_load_file(WIFI_KNOWN_NETWORKS_FILE,&check_n);
    if(!check || check_n!=strlen(encoded) || memcmp(check,encoded,check_n)!=0 || !guard(user)) goto done;
    memcpy(cfg.sta.ssid,ssid,strlen(ssid)); memcpy(cfg.sta.password,password,strlen(password));
    cfg.sta.threshold.authmode=password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable=true;
    if(!guard(user)) goto done;
    s_is_connected=false; /* old GOT_IP cannot satisfy the new setup request */
    (void)esp_wifi_disconnect();
    if(!guard(user) || esp_wifi_set_config(WIFI_IF_STA,&cfg)!=ESP_OK || !guard(user)) goto done;
    wifi_config_t actual={0};
    esp_err_t readback=esp_wifi_get_config(WIFI_IF_STA,&actual);
    bool same=readback==ESP_OK && memcmp(actual.sta.ssid,cfg.sta.ssid,sizeof(cfg.sta.ssid))==0 &&
        memcmp(actual.sta.password,cfg.sta.password,sizeof(cfg.sta.password))==0;
    muse_secret_wipe(&actual,sizeof(actual));
    if(!same || !guard(user) || esp_wifi_connect()!=ESP_OK || !guard(user)) goto done;
    ok=true;
done:
    if(temp_created) (void)unlink(temp_path); /* only this module's failed staging file */
    if(original) { muse_secret_wipe(original,original_n); free(original); }
    if(check) { muse_secret_wipe(check,check_n); free(check); }
    if(encoded) { muse_secret_wipe(encoded,strlen(encoded)); free(encoded); }
    muse_json_wipe(root); muse_secret_wipe(&cfg,sizeof(cfg));
    xSemaphoreGive(s_mutex); return ok;
}
