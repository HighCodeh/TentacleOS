// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NFC_CAPABILITIES_H
#define NFC_CAPABILITIES_H

#include <stdbool.h>
#include <stdint.h>

#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_CAPABILITY_MAX_ACTIONS 8

/** Actions that the UI can offer after a physical card has been identified. */
typedef enum {
  NFC_CARD_ACTION_NONE = 0,
  NFC_CARD_ACTION_MFC_DUMP,
  NFC_CARD_ACTION_MFC_NDEF_READ,
  NFC_CARD_ACTION_KEY_DICTIONARY,
  NFC_CARD_ACTION_ULTRALIGHT_READ,
  NFC_CARD_ACTION_NDEF_READ,
  NFC_CARD_ACTION_APDU,
  NFC_CARD_ACTION_PAYMENT_INFO,
  NFC_CARD_ACTION_DESFIRE_INFO,
  NFC_CARD_ACTION_ISO15693_READ,
  NFC_CARD_ACTION_FELICA_READ,
  NFC_CARD_ACTION_WRITE_DUMP,
  NFC_CARD_ACTION_EMULATE_DUMP,
  NFC_CARD_ACTION_SAVE,
  NFC_CARD_ACTION_RESCAN,
} nfc_card_action_id_t;

typedef struct {
  nfc_card_action_id_t id;
  const char *label;
  bool needs_tag;
  bool modifies_tag;
} nfc_card_action_t;

typedef struct {
  char family[32];
  char standard[32];
  char evidence[48];
  nfc_card_action_t actions[NFC_CAPABILITY_MAX_ACTIONS];
  uint8_t action_count;
} nfc_card_capabilities_t;

/** Build a conservative capability set from observed protocol evidence. */
void nfc_capabilities_build(const hb_nfc_card_data_t *card, nfc_card_capabilities_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NFC_CAPABILITIES_H */
