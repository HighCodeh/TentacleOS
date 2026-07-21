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

#ifndef NFC_READ_UI_H
#define NFC_READ_UI_H

/**
 * @brief Simulated tag reader: animated field scan that "finds" a tag and shows
 *        its UID/type/ATQA/SAK; OK saves it to the library.
 */
void ui_nfc_read_open(void);

#endif // NFC_READ_UI_H
