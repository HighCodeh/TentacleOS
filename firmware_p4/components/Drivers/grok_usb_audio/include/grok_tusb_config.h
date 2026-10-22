// SPDX-License-Identifier: GPL-3.0-or-later
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

#ifndef FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_TUSB_CONFIG_H
#define FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_TUSB_CONFIG_H
// Project-owned overlay: never edit generated sdkconfig or managed components.
#include "../../../../managed_components/espressif__esp_tinyusb/include/tusb_config.h"

#define CFG_TUD_AUDIO                    1
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN    237
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ 64
#define CFG_TUD_AUDIO_ENABLE_EP_IN       1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT      1
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1
// USB-spec 10.14 at full speed (macOS), 16.16 at high speed.
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_FORMAT_CORRECTION 1
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX               34
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX              34
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ            1280
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ           1280

#endif // FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_TUSB_CONFIG_H
