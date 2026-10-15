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

#ifndef HIGHBOY_NFC_COMPAT_H
#define HIGHBOY_NFC_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"
#include "st25r3916_cmd.h"

/* ---- Type aliases ---- */

typedef esp_err_t hb_nfc_err_t;
typedef highboy_nfc_protocol_t hb_nfc_protocol_t;
typedef highboy_nfc_iso14443a_t nfc_iso14443a_data_t;
typedef highboy_nfc_iso14443b_t nfc_iso14443b_data_t;
typedef highboy_nfc_iso_dep_t nfc_iso_dep_data_t;
typedef highboy_nfc_iso15693_t nfc_iso15693_data_t;
typedef highboy_nfc_felica_t nfc_felica_data_t;
typedef highboy_nfc_card_t hb_nfc_card_data_t;
typedef highboy_nfc_mf_key_type_t mf_key_type_t;

/* ---- Success / error short-forms ---- */

#define HB_NFC_OK             ESP_OK
#define HB_NFC_ERR_PARAM      ESP_ERR_INVALID_ARG
#define HB_NFC_ERR_INTERNAL   ESP_FAIL
#define HB_NFC_ERR_PROTOCOL   (HIGHBOY_NFC_ERR_BASE + 0x20)
#define HB_NFC_ERR_AUTH       HIGHBOY_NFC_ERR_AUTH
#define HB_NFC_ERR_NO_CARD    HIGHBOY_NFC_ERR_NO_CARD
#define HB_NFC_ERR_NOT_FOUND  ESP_ERR_NOT_FOUND
#define HB_NFC_ERR_TIMEOUT    ESP_ERR_TIMEOUT
#define HB_NFC_ERR_NACK       (HIGHBOY_NFC_ERR_BASE + 0x22)
#define HB_NFC_ERR_TX_TIMEOUT ESP_ERR_TIMEOUT
#define HB_NFC_ERR_CRC        HIGHBOY_NFC_ERR_CRC
#define HB_NFC_ERR_COLLISION  HIGHBOY_NFC_ERR_COLLISION
#define HB_NFC_ERR_FIELD      HIGHBOY_NFC_ERR_FIELD

/* ---- Protocol enum aliases ---- */

#define HB_PROTO_UNKNOWN       HIGHBOY_NFC_PROTO_UNKNOWN
#define HB_PROTO_ISO14443_3A   HIGHBOY_NFC_PROTO_ISO14443_3A
#define HB_PROTO_ISO14443_3B   HIGHBOY_NFC_PROTO_ISO14443_3B
#define HB_PROTO_ISO14443_4A   HIGHBOY_NFC_PROTO_ISO14443_4A
#define HB_PROTO_ISO14443_4B   HIGHBOY_NFC_PROTO_ISO14443_4B
#define HB_PROTO_FELICA        HIGHBOY_NFC_PROTO_FELICA
#define HB_PROTO_ISO15693      HIGHBOY_NFC_PROTO_ISO15693
#define HB_PROTO_MF_CLASSIC    HIGHBOY_NFC_PROTO_MF_CLASSIC
#define HB_PROTO_MF_ULTRALIGHT HIGHBOY_NFC_PROTO_MF_ULTRALIGHT
#define HB_PROTO_MF_PLUS       HIGHBOY_NFC_PROTO_MF_PLUS
#define HB_PROTO_MF_DESFIRE    HIGHBOY_NFC_PROTO_MF_DESFIRE
#define HB_PROTO_ST25TB        HIGHBOY_NFC_PROTO_ST25TB
#define HB_PROTO_SLIX          HIGHBOY_NFC_PROTO_SLIX

/* ---- MIFARE key-type enum aliases ---- */

#define MF_KEY_A HIGHBOY_NFC_MF_KEY_A
#define MF_KEY_B HIGHBOY_NFC_MF_KEY_B

/* ---- Capacity constant aliases ---- */

#define NFC_UID_MAX_LEN HIGHBOY_NFC_UID_MAX_LEN
#define NFC_ATS_MAX_LEN HIGHBOY_NFC_ATS_MAX_LEN

/* --- ISO 14443-A SELECT cascade aliases --- */
#define SEL_CL1 ST25R3916_SEL_CL1
#define SEL_CL2 ST25R3916_SEL_CL2
#define SEL_CL3 ST25R3916_SEL_CL3

/* ---- MIFARE Classic types ---- */
#define MF_CLASSIC_KEY_LEN 6

typedef struct {
  uint8_t data[MF_CLASSIC_KEY_LEN];
} mf_classic_key_t;

typedef enum {
  MF_CLASSIC_MINI = 0,
  MF_CLASSIC_1K,
  MF_CLASSIC_4K,
  MF_CLASSIC_TYPE_COUNT
} mf_classic_type_t;

#define hb_nfc_err_str(e) esp_err_to_name((e))

#ifdef __cplusplus
}
#endif

#endif /* HIGHBOY_NFC_COMPAT_H */
