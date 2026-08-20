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

#ifndef USB_STORAGE_UI_H
#define USB_STORAGE_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the USB Storage screen: exposes the microSD to a host PC as a USB
 *        drive. Reached from the Files root. BACK ejects and returns to Files
 *        WITHOUT rebooting.
 */
void ui_usb_storage_open(void);

/** @brief Screen close hook: reboots only if it is torn down while the card is
 *         still exposed to the host (defensive; normal exit needs no reboot). */
void ui_usb_storage_stop(void);

#ifdef __cplusplus
}
#endif

#endif // USB_STORAGE_UI_H
