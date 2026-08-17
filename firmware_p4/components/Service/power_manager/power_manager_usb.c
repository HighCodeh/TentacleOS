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

// TinyUSB bus-lifecycle callbacks (item 41b). TinyUSB declares these weak; the
// strong definitions here override them and route the USB bus state into the
// power manager. They only fire when the native P4 USB is up (mux switched to
// native); on the default UART-bridge path TinyUSB is not installed, so the
// device's plugged-in state comes solely from VBUS (item 41a).
//
// Mapping to the power manager's "USB suspended" input:
//   mounted / resumed  -> bus live  -> not suspended -> hold the wake lock
//   host suspend / unmount -> bus idle -> suspended  -> release the wake lock

#include "tusb.h"

#include "power_manager.h"

void tud_mount_cb(void) {
  power_manager_set_usb_suspended(false);
}

void tud_umount_cb(void) {
  power_manager_set_usb_suspended(true);
}

void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  power_manager_set_usb_suspended(true);
}

void tud_resume_cb(void) {
  power_manager_set_usb_suspended(false);
}
