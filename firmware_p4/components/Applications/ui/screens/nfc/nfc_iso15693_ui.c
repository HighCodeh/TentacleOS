// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nfc_iso15693_ui.h"

#include "nfc_protocol_lab_ui.h"

void ui_nfc_iso15693_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_ISO15693, "NFC-V / 15693");
}
