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
#define MF_KEY_SIZE 6
#define MF_KEY_DICT_MAX_KEYS 4
#define HB_NFC_OK 0
#define HB_NFC_ERR_INTERNAL -1
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define SD_USER_DIC "test.dic"
typedef int hb_nfc_err_t;
static uint8_t s_keys[MF_KEY_DICT_MAX_KEYS][MF_KEY_SIZE];
static int s_count,writes;
static bool fail_write;
static bool mf_key_dict_contains(const uint8_t key[MF_KEY_SIZE]) {
 for(int i=0;i<s_count;i++)if(!memcmp(s_keys[i],key,MF_KEY_SIZE))return true;
 return false;
}
static int nfc_dict_append_key(const char *path,const uint8_t *key,size_t size) {
 (void)path;(void)key;assert(size==MF_KEY_SIZE);writes++;return fail_write?-1:0;
}
/* PRODUCTION */
int main(void) {
 const uint8_t key[MF_KEY_SIZE]={1,2,3,4,5,6};
 fail_write=true;assert(mf_key_dict_add(key)!=HB_NFC_OK && writes==1);
 assert(!mf_key_dict_contains(key) && s_count==0);
 fail_write=false;assert(mf_key_dict_add(key)==HB_NFC_OK && writes==2 && s_count==1);
 assert(mf_key_dict_add(key)==HB_NFC_OK && writes==2 && s_count==1);
 puts("PASS: failed SD write cannot masquerade as a saved key on retry; durable duplicates remain idempotent");
}
