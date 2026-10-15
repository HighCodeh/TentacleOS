// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NFC_PROTOCOL_SERVICE_H
#define NFC_PROTOCOL_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "highboy_nfc_compat.h"

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

hb_nfc_err_t nfc_protocol_service_start(nfc_protocol_op_t operation);
void nfc_protocol_service_cancel(void);
void nfc_protocol_service_stop(void);
void nfc_protocol_service_get_status(nfc_protocol_status_t *out);
const char *nfc_protocol_operation_name(nfc_protocol_op_t operation);

#ifdef __cplusplus
}
#endif

#endif /* NFC_PROTOCOL_SERVICE_H */
