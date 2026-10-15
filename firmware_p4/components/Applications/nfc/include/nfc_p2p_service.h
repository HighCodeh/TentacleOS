// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NFC_P2P_SERVICE_H
#define NFC_P2P_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NFC_P2P_PAYLOAD_TEXT = 0,
  NFC_P2P_PAYLOAD_URI,
  NFC_P2P_PAYLOAD_COUNT,
} nfc_p2p_payload_t;

typedef enum {
  NFC_P2P_STATE_IDLE = 0,
  NFC_P2P_STATE_INITIALIZING,
  NFC_P2P_STATE_SEARCHING,
  NFC_P2P_STATE_LLCP_CONNECTED,
  NFC_P2P_STATE_SNEP_SENDING,
  NFC_P2P_STATE_DONE,
  NFC_P2P_STATE_ERROR,
  NFC_P2P_STATE_CANCELLED,
} nfc_p2p_state_t;

typedef struct {
  nfc_p2p_state_t state;
  hb_nfc_err_t error;
  nfc_p2p_payload_t payload;
  uint16_t negotiated_miu;
  uint16_t attempts;
  uint16_t ndef_len;
  uint8_t snep_response;
  bool running;
} nfc_p2p_status_t;

/** Start a real NFC-DEP/LLCP/SNEP initiator transfer. */
hb_nfc_err_t nfc_p2p_service_start(nfc_p2p_payload_t payload);

/** Request cancellation of an active transfer. */
void nfc_p2p_service_cancel(void);

/** Cancel and wait for the radio task to stop. */
void nfc_p2p_service_stop(void);

/** Copy the latest task status. */
void nfc_p2p_service_get_status(nfc_p2p_status_t *out);

/** Human-readable payload label. */
const char *nfc_p2p_payload_name(nfc_p2p_payload_t payload);

#ifdef __cplusplus
}
#endif

#endif /* NFC_P2P_SERVICE_H */
