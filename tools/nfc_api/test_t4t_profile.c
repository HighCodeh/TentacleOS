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

#include "nfc_t4t.h"
#include "mf_desfire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int storage_write_atomic(const char *path, const void *data, size_t size) {
  FILE *file = fopen(path, "wb"); assert(file);
  assert(fwrite(data, 1, size, file) == size); return fclose(file);
}
static unsigned exchange(nfc_t4t_listener_t *listener, const uint8_t *bytes, size_t size) {
  nfc_t4t_request_t request = {bytes, size}; nfc_t4t_response_t response;
  assert(nfc_t4t_exchange(listener, &request, &response) == ESP_OK);
  return (response.data[response.length-2] << 8) | response.data[response.length-1];
}
static unsigned commands;
static bool is_wrong_product, is_cancelled, is_existing, is_blank_cc, is_bad_settings;
static unsigned write_commands;
static bool cancelled(void) {return is_cancelled;}
int mf_desfire_poller_init(const nfc_iso14443a_data_t *id, mf_desfire_session_t *session) {(void)id;(void)session;return 0;}
int mf_desfire_get_version(mf_desfire_session_t *session,mf_desfire_version_t *v) {
  (void)session; memset(v,0,sizeof(*v));v->hw[0]=4;v->hw[1]=is_wrong_product?4:1;return 0;
}
int mf_desfire_select_application(mf_desfire_session_t *session,const mf_desfire_aid_t *aid) {(void)session;(void)aid;return 0;}
int mf_desfire_transceive_native(mf_desfire_session_t *session,uint8_t cmd,const uint8_t *data,size_t len,
    uint8_t *out,size_t capacity,size_t *received,uint16_t *sw) {
  (void)out;(void)capacity;
  if(session->should_cancel && session->should_cancel()) return ESP_ERR_INVALID_STATE;
  commands++; *received=0; *sw=0x9100;
  if(cmd==0xCA) {assert(len==14 && data[4]==0x21);if(is_existing)*sw=0x91DE;}
  else if(cmd==0xCD) {assert(len==9);if(is_existing)*sw=0x91DE;}
  else if(cmd==0xF5) {
    assert(is_existing && len==1 && capacity>=7);
    uint8_t settings[]={0,0,0xEE,0xEE,15,0,0};
    if(data[0]==2) {settings[2]=0xE0;settings[4]=0;settings[5]=8;}
    if(is_bad_settings)settings[0]=1;
    memcpy(out,settings,7);*received=7;
  } else if(cmd==0xBD) {
    assert(is_existing && len==7 && capacity>=15);
    const uint8_t cc[]={0,15,0x20,0,0x3A,0,0x34,4,6,0xE1,4,8,0,0,0};
    memcpy(out,cc,15);if(is_blank_cc)memset(out,0,15);*received=15;
  } else if(cmd==0x3D) {
    write_commands++;
    if(data[0]==1) assert(len==22 && data[7]==0 && data[8]==15);
    else assert(!is_existing && len==9 && data[7]==0 && data[8]==0);
  } else assert(0);
  return 0;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  nfc_t4t_card_t *card = malloc(sizeof(*card)), *loaded = malloc(sizeof(*loaded));
  assert(card && loaded); nfc_t4t_defaults(card);
  card->file[1] = 40; for (unsigned i = 0; i < 40; i++) card->file[i+2] = i;
  assert(nfc_t4t_save(argv[1], card) == ESP_OK);
  assert(nfc_t4t_load(argv[1], loaded) == ESP_OK);
  assert(!memcmp(card, loaded, sizeof(*card)));
  card->cc.ndef_max = NFC_T4T_FILE_SIZE;
  card->file[0] = 0x7F; card->file[1] = 0xFE;
  memset(card->file + 2, 0xA5, NFC_T4T_FILE_SIZE - 2);
  assert(nfc_t4t_save(argv[1], card) == ESP_OK);
  assert(nfc_t4t_load(argv[1], loaded) == ESP_OK && !memcmp(card, loaded, sizeof(*card)));
  nfc_t4t_defaults(loaded);
  FILE *fork_file = fopen(argv[1], "w"); assert(fork_file);
  fputs("Filetype: Flipper NFC device\nVersion: 4\nDevice type: Type 4 Tag\nUID: 04 01 02 03\nATQA: 03 44\nSAK: 20\nT0: 48\nTC(1): 00\nNDEF Data Size: 3\nNDEF Data: D0 00 00\n", fork_file);
  fclose(fork_file);
  assert(nfc_t4t_load(argv[1], loaded) == ESP_OK && nfc_t4t_length(loaded) == 3 && loaded->cc.ndef_max == 2048);
  nfc_t4t_listener_t listener = {.card=loaded};
  const uint8_t aid[] = {0,0xA4,4,0,7,0xD2,0x76,0,0,0x85,1,1,0};
  const uint8_t file[] = {0,0xA4,0,0x0C,2,0xE1,4};
  const uint8_t read[] = {0,0xB0,0,0,2};
  const uint8_t zero[] = {0,0xD6,0,0,2,0,0};
  assert(exchange(&listener, read, sizeof(read)) == 0x6982);
  assert(exchange(&listener, aid, sizeof(aid)) == 0x9000);
  assert(exchange(&listener, file, sizeof(file)) == 0x9000);
  loaded->cc.write_access = 0xFF;
  assert(exchange(&listener, zero, sizeof(zero)) == 0x6982);
  loaded->cc.write_access = 0;
  assert(exchange(&listener, zero, sizeof(zero)) == 0x9000);
  assert(listener.is_dirty && !nfc_t4t_length(loaded));
  const uint8_t bad[] = {0,0xD6,0,0,2,0xFF,0xFF};
  assert(exchange(&listener, bad, sizeof(bad)) == 0x6700);
  assert(!nfc_t4t_length(loaded));
  nfc_t4t_listener_reset(&listener);
  assert(exchange(&listener, read, sizeof(read)) == 0x6982);
  assert(listener.is_dirty);
  assert(nfc_t4t_save(argv[1], loaded) == ESP_OK);
  assert(nfc_t4t_load(argv[1], card) == ESP_OK && !nfc_t4t_length(card));
  FILE *append = fopen(argv[1], "a"); assert(append);
  fputs("NDEF Data Size: 0\n", append); fclose(append);
  assert(nfc_t4t_load(argv[1], card) == ESP_ERR_INVALID_RESPONSE);
  nfc_iso14443a_data_t identity = {0};
  assert(nfc_t4t_create(&identity,cancelled)==ESP_OK && commands==5);
  commands=0;is_wrong_product=true;
  assert(nfc_t4t_create(&identity,cancelled)==ESP_ERR_NOT_SUPPORTED && !commands);
  is_wrong_product=false;is_existing=true;
  write_commands=0;
  assert(nfc_t4t_create(&identity,cancelled)==ESP_OK && commands==6 && !write_commands);
  commands=0;is_blank_cc=true;
  assert(nfc_t4t_create(&identity,cancelled)==ESP_OK && commands==7 && write_commands==1);
  commands=0;write_commands=0;is_bad_settings=true;
  assert(nfc_t4t_create(&identity,cancelled)==ESP_ERR_INVALID_RESPONSE && !write_commands);
  is_bad_settings=false;is_blank_cc=false;
  commands=0;is_existing=false;is_cancelled=true;
  assert(nfc_t4t_create(&identity,cancelled)==ESP_ERR_INVALID_STATE && !commands);
  free(card); free(loaded);
  puts("Type 4 profile/listener tests passed");
}
