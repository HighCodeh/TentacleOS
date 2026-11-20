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

#ifndef NFC_EMV_H
#define NFC_EMV_H
#include "emv.h"
#include "t4t_emu.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  t4t_emu_identity_t identity;
  emv_profile_t profile;
} nfc_emv_card_t;
esp_err_t nfc_emv_load(const char *path, nfc_emv_card_t *out_card);
esp_err_t nfc_emv_save(const char *path, const nfc_emv_card_t *card);
#ifdef __cplusplus
}
#endif
#endif
