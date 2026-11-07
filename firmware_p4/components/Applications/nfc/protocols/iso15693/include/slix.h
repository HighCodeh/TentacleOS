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
/**
 * @file iso15693.c
 * @brief ISO 15693 (NFC-V) poller implementation.
 */
#ifndef SLIX_H
#define SLIX_H

#include "iso15693.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SLIX_SIGNATURE_BYTES 32U
#define SLIX_PASSWORD_BYTES  4U

typedef enum {
  SLIX_PASSWORD_READ,
  SLIX_PASSWORD_WRITE,
  SLIX_PASSWORD_PRIVACY,
  SLIX_PASSWORD_DESTROY,
  SLIX_PASSWORD_EAS_AFI,
  SLIX_PASSWORD_COUNT,
} slix_password_t;

typedef struct {
  uint8_t protection_pointer;
  uint8_t protection_condition;
  uint8_t lock_bits;
  uint32_t features;
} slix_system_info_t;

hb_nfc_err_t slix_get_system_info(const iso15693_tag_t *tag, slix_system_info_t *out_info);
hb_nfc_err_t slix_read_signature(const iso15693_tag_t *tag, uint8_t *out_signature);
/* Authenticates the current session; does not write or replace a card password.
 * Privacy uses an unaddressed exchange and accepts a NULL tag. */
hb_nfc_err_t slix_authenticate(const iso15693_tag_t *tag,
                               slix_password_t type,
                               const uint8_t password[SLIX_PASSWORD_BYTES]);

#ifdef __cplusplus
}
#endif
#endif
