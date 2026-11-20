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

#ifndef NFC_T4T_H
#define NFC_T4T_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "t4t_emu.h"
#include "t4t.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NFC_T4T_FILE_SIZE     32768U
#define NFC_T4T_DEFAULT_SIZE  2048U
#define NFC_T4T_CC_SIZE       15U
#define NFC_T4T_RESPONSE_SIZE 258U

typedef struct {
  t4t_emu_identity_t identity;
  t4t_cc_t cc;
  uint8_t file[NFC_T4T_FILE_SIZE];
} nfc_t4t_card_t;
typedef struct nfc_t4t_listener {
  nfc_t4t_card_t *card;
  bool is_selected;
  bool is_dirty;
  uint16_t selected_file;
} nfc_t4t_listener_t;
typedef struct {
  const uint8_t *data;
  size_t length;
} nfc_t4t_request_t;
typedef struct {
  uint8_t data[NFC_T4T_RESPONSE_SIZE];
  size_t length;
} nfc_t4t_response_t;

void nfc_t4t_defaults(nfc_t4t_card_t *card);
size_t nfc_t4t_length(const nfc_t4t_card_t *card);
esp_err_t nfc_t4t_load(const char *path, nfc_t4t_card_t *out_card);
esp_err_t nfc_t4t_save(const char *path, const nfc_t4t_card_t *card);
void nfc_t4t_listener_reset(nfc_t4t_listener_t *listener);
esp_err_t nfc_t4t_exchange(nfc_t4t_listener_t *listener,
                           const nfc_t4t_request_t *request,
                           nfc_t4t_response_t *out_response);
/* No deletion or key changes: create the reference NDEF application on DESFire only. */
esp_err_t nfc_t4t_create(const nfc_iso14443a_data_t *identity, bool (*should_cancel)(void));
#ifdef __cplusplus
}
#endif
#endif
