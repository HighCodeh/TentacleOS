// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "nfc_store.h"

static const char *TAG = "NFC_STORE";

void nfc_store_init(void) {}
int nfc_store_count(void) {
  return 0;
}
hb_nfc_err_t nfc_store_save(const nfc_store_card_t *card) {
  (void)card;
  return HB_NFC_ERR_INTERNAL;
}
hb_nfc_err_t nfc_store_load(int i, nfc_store_entry_t *o) {
  (void)i;
  (void)o;
  return HB_NFC_ERR_NOT_FOUND;
}
hb_nfc_err_t nfc_store_get_info(int i, nfc_store_info_t *o) {
  (void)i;
  (void)o;
  return HB_NFC_ERR_NOT_FOUND;
}
int nfc_store_list(nfc_store_info_t *infos, int max) {
  (void)infos;
  (void)max;
  return 0;
}
hb_nfc_err_t nfc_store_delete(int i) {
  (void)i;
  return HB_NFC_ERR_NOT_FOUND;
}
hb_nfc_err_t nfc_store_find_by_uid(const uint8_t *uid, uint8_t len, int *out) {
  (void)uid;
  (void)len;
  (void)out;
  return HB_NFC_ERR_NOT_FOUND;
}
size_t nfc_store_pack_mful(const nfc_mful_pack_t *in, uint8_t *out, size_t max) {
  (void)in;
  (void)out;
  (void)max;
  return 0;
}
bool nfc_store_unpack_mful(const uint8_t *payload, size_t payload_len, nfc_mful_unpack_t *out) {
  (void)payload;
  (void)payload_len;
  (void)out;
  return false;
}
