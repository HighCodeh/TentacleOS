#pragma once
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
