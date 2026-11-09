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

/**
 * @file usb_msc.h
 * @brief USB Mass Storage mode: expose the microSD to a host PC as a USB drive.
 */
#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB Mass Storage mode state.
 */
typedef enum {
  USB_MSC_IDLE = 0,       /**< Not in USB-storage mode. */
  USB_MSC_ENTERING,       /**< Detaching SD / bringing USB up. */
  USB_MSC_ACTIVE,         /**< SD exposed to the host as a USB drive. */
  USB_MSC_ERROR,          /**< Entry failed; no host owns the card. */
  USB_MSC_EXITING,        /**< Tearing down / remounting SD. */
  USB_MSC_SD_UNAVAILABLE, /**< USB stopped; SD quarantined until restart. */
} usb_msc_state_t;

/**
 * @brief Get the current USB Mass Storage mode state (poll this from the UI).
 *
 * @return The current ::usb_msc_state_t value.
 */
usb_msc_state_t usb_msc_get_state(void);

/**
 * @brief Check whether USB is enumerated (not whether the filesystem is mounted).
 *
 * @return true while USB is enumerated in storage mode, false otherwise.
 */
bool usb_msc_host_connected(void);

/**
 * @brief Enter USB Mass Storage mode: detach the app's /sdcard FAT, expose the
 *        raw card to the host, and switch the USB mux to native. On failure the
 *        SD is restored. Blocking — run on a worker task, NEVER the LVGL thread.
 */
void usb_msc_enter(void);

/**
 * @brief Leave USB Mass Storage mode and RESUME normal operation (no reboot):
 *        Caller must obtain confirmation that the host volume was ejected.
 *        Stop exposing the card, remount /sdcard for the app, and route the USB
 *        connector back to the UART bridge. A failed recovery leaves SD unavailable until restart;
 * UI may leave safely. Blocking — run on a worker task.
 */
void usb_msc_exit(void);

#ifdef __cplusplus
}
#endif

#endif // USB_MSC_H
