// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NFC_PROTOCOL_LAB_UI_H
#define NFC_PROTOCOL_LAB_UI_H

#include "nfc_protocol_service.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_nfc_protocol_lab_open(nfc_protocol_op_t operation, const char *title);
void ui_nfc_protocol_lab_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_PROTOCOL_LAB_UI_H */
