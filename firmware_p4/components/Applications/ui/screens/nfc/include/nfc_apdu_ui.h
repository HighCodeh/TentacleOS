// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NFC_APDU_UI_H
#define NFC_APDU_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/** Open the persistent ISO-DEP APDU Protocol Workbench. */
void ui_nfc_apdu_open(void);

/** Stop the workbench worker and release its RF/power resources. */
void ui_nfc_apdu_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_APDU_UI_H */
