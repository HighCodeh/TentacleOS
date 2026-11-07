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

#ifndef NFC_PROTOCOL_SERVICE_H
#define NFC_PROTOCOL_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "highboy_nfc_compat.h"
#include "iso15693_listener.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_PROTOCOL_RESULT_LINES    7
#define NFC_PROTOCOL_RESULT_LINE_LEN 64

typedef enum {
  NFC_PROTOCOL_OP_DESFIRE = 0,
  NFC_PROTOCOL_OP_ISO15693,
  NFC_PROTOCOL_OP_ULTRALIGHT,
  NFC_PROTOCOL_OP_NDEF,
  NFC_PROTOCOL_OP_FELICA,
  NFC_PROTOCOL_OP_ST25TB,
  NFC_PROTOCOL_OP_SLIX,
  NFC_PROTOCOL_OP_NTAG4XX,
  NFC_PROTOCOL_OP_TEXKOM,
  NFC_PROTOCOL_OP_TEXKOM_EMULATE,
  NFC_PROTOCOL_OP_ST25TB_WRITE,
  NFC_PROTOCOL_OP_V_EMULATE,
  NFC_PROTOCOL_OP_PLUS,
  NFC_PROTOCOL_OP_PLUS_WRITE,
  NFC_PROTOCOL_OP_UL_AES_WRITE,
  NFC_PROTOCOL_OP_SLIX_WRITE,
  NFC_PROTOCOL_OP_ARCHIVE,
  NFC_PROTOCOL_OP_ARCHIVE_EXPORT,
  NFC_PROTOCOL_OP_DESFIRE_LOAD,
  NFC_PROTOCOL_OP_DESFIRE_EMULATE,
  NFC_PROTOCOL_OP_FELICA_LOAD,
  NFC_PROTOCOL_OP_FELICA_EMULATE,
  NFC_PROTOCOL_OP_NTAG4XX_LOAD,
  NFC_PROTOCOL_OP_NTAG4XX_EMULATE,
  NFC_PROTOCOL_OP_T4T_LOAD,
  NFC_PROTOCOL_OP_T4T_EMULATE,
  NFC_PROTOCOL_OP_T4T_WRITE,
  NFC_PROTOCOL_OP_T4T_CREATE,
  NFC_PROTOCOL_OP_COUNT,
} nfc_protocol_op_t;

typedef enum {
  NFC_PROTOCOL_STATE_IDLE = 0,
  NFC_PROTOCOL_STATE_RUNNING,
  NFC_PROTOCOL_STATE_DONE,
  NFC_PROTOCOL_STATE_ERROR,
  NFC_PROTOCOL_STATE_CANCELLED,
} nfc_protocol_state_t;

typedef struct {
  char heading[32];
  char lines[NFC_PROTOCOL_RESULT_LINES][NFC_PROTOCOL_RESULT_LINE_LEN];
  uint8_t line_count;
} nfc_protocol_result_t;

typedef struct {
  nfc_protocol_op_t operation;
  nfc_protocol_state_t state;
  hb_nfc_err_t error;
  nfc_protocol_result_t result;
  bool running;
} nfc_protocol_status_t;

/* DESFire: filename under dumps; view pages only while the worker is idle. */
hb_nfc_err_t nfc_protocol_service_desfire_file(const char *filename);
esp_err_t
nfc_protocol_service_desfire_page(size_t page, char *out, size_t capacity, size_t *out_pages);
void nfc_protocol_service_desfire_clear(void);

/* NTAG4xx typed profiles; emulation exposes ISO-DEP identity only. */
hb_nfc_err_t nfc_protocol_service_t4t_file(const char *filename);
hb_nfc_err_t nfc_protocol_service_ntag4xx_file(const char *filename);
esp_err_t
nfc_protocol_service_ntag4xx_page(size_t page, char *out, size_t capacity, size_t *out_pages);
void nfc_protocol_service_ntag4xx_clear(void);

/* FeliCa saved profiles and optional volatile Lite key (32 hex digits, NULL clears). */
hb_nfc_err_t nfc_protocol_service_felica_file(const char *filename);
hb_nfc_err_t nfc_protocol_service_felica_key(const char *text);
esp_err_t
nfc_protocol_service_felica_page(size_t page, char *out, size_t capacity, size_t *out_pages);
void nfc_protocol_service_felica_clear(void);

/* Plus: one volatile AES candidate; NULL clears it. */
hb_nfc_err_t nfc_protocol_service_plus_key(const char *text);
/* Stage a dump filename from /sdcard/nfc/dumps; empty selects the last read. */
hb_nfc_err_t nfc_protocol_service_plus_write(const char *filename);
/* Stage AES user-memory write; key: prefix also copies DataProtKey. NULL cancels. */
hb_nfc_err_t nfc_protocol_service_ul_aes_write(const char *filename);

/* 0:<32 hex> plain or 1:<32 hex> CMAC; NULL clears the volatile key. */
hb_nfc_err_t nfc_protocol_service_ul_aes_key(const char *text);
/* Stage a matching-UID data restore; NULL cancels, empty uses last saved read. */
hb_nfc_err_t nfc_protocol_service_archive(const char *filename);

hb_nfc_err_t nfc_protocol_service_slix_write(const char *filename);

/* Eight hex digits in wire order; optional r:/w:/p:/d:/e: purpose, NULL clears all. */
hb_nfc_err_t nfc_protocol_service_slix_password(const char *hex);
/* Stage an ordinary-block write as BB:DDDDDDDD after a successful ST25TB read.
 * NULL clears the pending write. Starting ST25TB_WRITE consumes it once. */
hb_nfc_err_t nfc_protocol_service_st25tb_write(const char *hex);
hb_nfc_err_t nfc_protocol_service_select_texkom(int index);
hb_nfc_err_t nfc_protocol_service_select_v(int index);
/* Copies a Flipper NFC-V profile. Writes, when enabled, affect session RAM only. */
hb_nfc_err_t nfc_protocol_service_select_v_file(const char *path, bool allow_writes);
hb_nfc_err_t nfc_protocol_service_start(nfc_protocol_op_t operation);
void nfc_protocol_service_cancel(void);
void nfc_protocol_service_stop(void);
esp_err_t nfc_protocol_service_get_v_result(iso15693_listener_card_t *out_card);
void nfc_protocol_service_get_status(nfc_protocol_status_t *out);
const char *nfc_protocol_operation_name(nfc_protocol_op_t operation);

#ifdef __cplusplus
}
#endif

#endif /* NFC_PROTOCOL_SERVICE_H */
