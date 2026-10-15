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

#ifndef NFC_EMULATE_UI_H
#define NFC_EMULATE_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Open the real NFC emulation screen backed by saved MIFARE dumps. */
void ui_nfc_emulate_open(void);

/** @brief Stop any active RF emulation owned by the screen. */
void ui_nfc_emulate_stop(void);

#ifdef __cplusplus
}
#endif

#endif // NFC_EMULATE_UI_H
