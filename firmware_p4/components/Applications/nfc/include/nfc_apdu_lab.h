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

#ifndef NFC_APDU_LAB_H
#define NFC_APDU_LAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "highboy_nfc_compat.h"
#include "nfc_apdu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_APDU_LAB_TX_MAX      256U
#define NFC_APDU_LAB_RX_MAX      768U
#define NFC_APDU_LAB_HISTORY_MAX 12U
#define NFC_APDU_LAB_TRACE_PATH  "/nfc/apdu_trace.log"

typedef enum {
  NFC_APDU_LAB_STOPPED = 0,
  NFC_APDU_LAB_STARTING,
  NFC_APDU_LAB_SEARCHING,
  NFC_APDU_LAB_ACTIVATING,
  NFC_APDU_LAB_READY,
  NFC_APDU_LAB_EXCHANGING,
  NFC_APDU_LAB_ERROR,
  NFC_APDU_LAB_STOPPING,
} nfc_apdu_lab_state_t;

typedef struct {
  uint32_t sequence;
  uint8_t tx[NFC_APDU_LAB_TX_MAX];
  size_t tx_len;
  uint8_t rx[NFC_APDU_LAB_RX_MAX];
  size_t rx_len;
  uint16_t sw;
  nfc_apdu_risk_t risk;
  hb_nfc_err_t error;
  uint32_t elapsed_us;
  uint8_t exchanges;
  bool le_corrected;
  bool truncated;
  bool session_recovered;
} nfc_apdu_lab_entry_t;

typedef struct {
  nfc_apdu_lab_state_t state;
  hb_nfc_err_t error;
  hb_nfc_protocol_t protocol;
  uint8_t identifier[10];
  uint8_t identifier_len;
  uint8_t ats[32];
  uint8_t ats_len;
  uint16_t frame_size;
  uint8_t fwi;
  uint8_t history_count;
  uint32_t latest_sequence;
  bool trace_enabled;
  uint16_t regulator_mv;
  uint8_t rf_amplitude;
  bool regulator_manual;
  bool regulator_current_limit;
} nfc_apdu_lab_status_t;

/** Start the persistent ISO-DEP APDU laboratory session. */
hb_nfc_err_t nfc_apdu_lab_start(void);

/** Stop the session, disable the RF field and release the no-sleep lock. */
void nfc_apdu_lab_stop(void);

/**
 * Queue a command for the active card.
 *
 * Commands classified as authentication, mutating or unknown require
 * confirmed=true. Payment-state commands are deliberately rejected.
 */
hb_nfc_err_t nfc_apdu_lab_submit(const uint8_t *apdu, size_t apdu_len, bool confirmed);

/** Read a compact session snapshot. */
void nfc_apdu_lab_get_status(nfc_apdu_lab_status_t *out);

/** Read a history entry, where newest_index=0 is the most recent exchange. */
bool nfc_apdu_lab_get_history(uint8_t newest_index, nfc_apdu_lab_entry_t *out);

/** Clear the in-memory history without deleting the SD trace. */
void nfc_apdu_lab_clear_history(void);

/** Return the persistent trace path used when an SD card is mounted. */
const char *nfc_apdu_lab_trace_path(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_APDU_LAB_H */
