// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nfc_capabilities.h"

#include <stdio.h>
#include <string.h>

#include "mf_classic.h"
#include "nfc_card_info.h"
#include "nfc_device.h"

static void add_action(nfc_card_capabilities_t *out,
                       nfc_card_action_id_t id,
                       const char *label,
                       bool needs_tag,
                       bool modifies_tag) {
  if (out->action_count >= NFC_CAPABILITY_MAX_ACTIONS)
    return;
  out->actions[out->action_count++] = (nfc_card_action_t){
      .id = id,
      .label = label,
      .needs_tag = needs_tag,
      .modifies_tag = modifies_tag,
  };
}

static void add_save_and_rescan(nfc_card_capabilities_t *out) {
  add_action(out, NFC_CARD_ACTION_SAVE, "Save identification", false, false);
  add_action(out, NFC_CARD_ACTION_RESCAN, "Read another tag", true, false);
}

static void build_type_a(const hb_nfc_card_data_t *card, nfc_card_capabilities_t *out) {
  const nfc_card_type_info_t info =
      nfc_card_info_identify(card->iso14443a.sak, card->iso14443a.atqa);
  strlcpy(out->family, info.name, sizeof(out->family));
  strlcpy(out->standard, "NFC-A / ISO14443", sizeof(out->standard));

  switch (card->protocol) {
    case HB_PROTO_MF_CLASSIC: {
      const mf_classic_type_t type = mf_classic_get_type(card->iso14443a.sak);
      if (type == MF_CLASSIC_1K) {
        add_action(out, NFC_CARD_ACTION_MFC_DUMP, "Read all 64 blocks", true, false);
        add_action(out, NFC_CARD_ACTION_MFC_NDEF_READ, "Read NDEF text", true, false);
        add_action(out, NFC_CARD_ACTION_KEY_DICTIONARY, "Key dictionary", false, false);
        add_action(out, NFC_CARD_ACTION_WRITE_DUMP, "Write saved dump", true, true);
        add_action(out, NFC_CARD_ACTION_EMULATE_DUMP, "Emulate saved dump", false, false);
      } else {
        add_action(out, NFC_CARD_ACTION_KEY_DICTIONARY, "Key dictionary", false, false);
      }
      break;
    }
    case HB_PROTO_MF_ULTRALIGHT:
      add_action(out, NFC_CARD_ACTION_ULTRALIGHT_READ, "Read pages + version", true, false);
      add_action(out, NFC_CARD_ACTION_NDEF_READ, "Read NDEF records", true, false);
      break;
    case HB_PROTO_MF_PLUS:
      strlcpy(out->family, "MIFARE Plus SL2", sizeof(out->family));
      break;
    case HB_PROTO_ISO14443_4A:
      strlcpy(out->family, "ISO-DEP Type A", sizeof(out->family));
      strlcpy(out->standard, "ISO14443-4A", sizeof(out->standard));
      add_action(out, NFC_CARD_ACTION_NDEF_READ, "Read Type 4 NDEF", true, false);
      add_action(out, NFC_CARD_ACTION_APDU, "APDU workbench", true, false);
      add_action(out, NFC_CARD_ACTION_PAYMENT_INFO, "Payment info (read-only)", true, false);
      add_action(out, NFC_CARD_ACTION_DESFIRE_INFO, "DESFire probe (read-only)", true, false);
      break;
    default:
      break;
  }
}

void nfc_capabilities_build(const hb_nfc_card_data_t *card, nfc_card_capabilities_t *out) {
  if (out == NULL)
    return;
  memset(out, 0, sizeof(*out));
  strlcpy(out->family, "Unknown NFC tag", sizeof(out->family));
  strlcpy(out->standard, "Identification only", sizeof(out->standard));
  strlcpy(out->evidence, "Protocol response received", sizeof(out->evidence));
  if (card == NULL)
    return;

  switch (card->protocol) {
    case HB_PROTO_ISO14443_3A:
    case HB_PROTO_ISO14443_4A:
    case HB_PROTO_MF_CLASSIC:
    case HB_PROTO_MF_ULTRALIGHT:
    case HB_PROTO_MF_PLUS:
    case HB_PROTO_MF_DESFIRE:
      build_type_a(card, out);
      if (card->protocol == HB_PROTO_ISO14443_4A) {
        if (card->data.iso_dep.ats_len > 0) {
          snprintf(out->evidence,
                   sizeof(out->evidence),
                   "SAK %02X + ATS %u bytes",
                   card->iso14443a.sak,
                   card->data.iso_dep.ats_len);
        } else {
          snprintf(out->evidence,
                   sizeof(out->evidence),
                   "SAK %02X; ATS not confirmed",
                   card->iso14443a.sak);
        }
      } else {
        snprintf(out->evidence,
                 sizeof(out->evidence),
                 "ATQA %02X%02X / SAK %02X",
                 card->iso14443a.atqa[0],
                 card->iso14443a.atqa[1],
                 card->iso14443a.sak);
      }
      break;
    case HB_PROTO_ISO14443_3B:
      strlcpy(out->family, "NFC-B card", sizeof(out->family));
      strlcpy(out->standard, "ISO14443-3B", sizeof(out->standard));
      strlcpy(out->evidence, "ATQB received", sizeof(out->evidence));
      break;
    case HB_PROTO_ISO14443_4B:
      strlcpy(out->family, "ISO-DEP Type B", sizeof(out->family));
      strlcpy(out->standard, "ISO14443-4B", sizeof(out->standard));
      strlcpy(out->evidence, "ATQB + ATTRIB confirmed", sizeof(out->evidence));
      add_action(out, NFC_CARD_ACTION_APDU, "APDU workbench", true, false);
      break;
    case HB_PROTO_FELICA:
      strlcpy(out->family, "FeliCa / NFC-F", sizeof(out->family));
      strlcpy(out->standard, "ISO18092 at 212 kbps", sizeof(out->standard));
      strlcpy(out->evidence, "SENSF_RES received", sizeof(out->evidence));
      add_action(out, NFC_CARD_ACTION_FELICA_READ, "Read systems + blocks", true, false);
      break;
    case HB_PROTO_ISO15693:
    case HB_PROTO_SLIX:
      strlcpy(out->family,
              card->protocol == HB_PROTO_SLIX ? "ICODE SLIX / NFC-V" : "ISO15693 / NFC-V",
              sizeof(out->family));
      strlcpy(out->standard, "ISO15693", sizeof(out->standard));
      if (card->data.iso15693.block_count > 0) {
        snprintf(out->evidence,
                 sizeof(out->evidence),
                 "Inventory + %u x %u-byte geometry",
                 card->data.iso15693.block_count,
                 card->data.iso15693.block_size);
      } else {
        strlcpy(out->evidence, "Inventory response received", sizeof(out->evidence));
      }
      add_action(out, NFC_CARD_ACTION_ISO15693_READ, "Read system + blocks", true, false);
      break;
    default:
      strlcpy(out->family, nfc_device_protocol_name(card->protocol), sizeof(out->family));
      break;
  }

  add_save_and_rescan(out);
}
