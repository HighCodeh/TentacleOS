// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TOS_API_NFC_SESSION_H
#define TOS_API_NFC_SESSION_H

#include "tos_api.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tos_api_nfc_session_start(const uint8_t *uid, uint8_t uid_len, uint32_t *out_session);
esp_err_t tos_api_nfc_session_stop(uint32_t session);
void tos_api_nfc_session_cancel_owner(void *task);

#ifdef __cplusplus
}
#endif
#endif
