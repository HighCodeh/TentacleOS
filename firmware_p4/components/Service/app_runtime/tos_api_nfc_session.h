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
