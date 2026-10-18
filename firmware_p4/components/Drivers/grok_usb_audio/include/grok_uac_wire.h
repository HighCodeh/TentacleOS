#ifndef FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_UAC_WIRE_H
#define FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_UAC_WIRE_H

#define GROK_UAC_RATE        16000
#define GROK_UAC_DESC_LEN    237
#define GROK_UAC_ITF_AC      3
#define GROK_UAC_ITF_SPK     4
#define GROK_UAC_ITF_MIC     5
#define GROK_UAC_EP_SPK      0x05
#define GROK_UAC_EP_MIC      0x85
#define GROK_UAC_EP_FB       0x86
#define GROK_UAC_CLOCK       1
#define GROK_UAC_SPK_INPUT   2
#define GROK_UAC_SPK_FEATURE 3
#define GROK_UAC_SPK_OUTPUT  4
#define GROK_UAC_MIC_INPUT   5
#define GROK_UAC_MIC_FEATURE 6
#define GROK_UAC_MIC_OUTPUT  7
#define GROK_UAC_MUTE_CTRL   (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS)
#define GROK_UAC_VOLUME_CTRL (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS)

// One control interface, speaker+feedback, microphone. Data period is 1 ms
// at BOTH speeds: interval=1 at FS and interval=4 at HS. +1 sample permits drift.
#define GROK_UAC_DESCRIPTOR(str, interval, fb_bytes)                                             \
  TUD_AUDIO_DESC_IAD(GROK_UAC_ITF_AC, 3, str), TUD_AUDIO_DESC_STD_AC(GROK_UAC_ITF_AC, 0, str),   \
      TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_HEADSET, 94, 0),                                   \
      TUD_AUDIO_DESC_CLK_SRC(GROK_UAC_CLOCK,                                                     \
                             AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,                                 \
                             (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) |             \
                                 (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_VAL_POS),          \
                             0,                                                                  \
                             0),                                                                 \
      TUD_AUDIO_DESC_INPUT_TERM(                                                                 \
          GROK_UAC_SPK_INPUT, AUDIO_TERM_TYPE_USB_STREAMING, 0, GROK_UAC_CLOCK, 1, 0, 0, 0, 0),  \
      TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL(GROK_UAC_SPK_FEATURE,                              \
                                              GROK_UAC_SPK_INPUT,                                \
                                              GROK_UAC_MUTE_CTRL | GROK_UAC_VOLUME_CTRL,         \
                                              0,                                                 \
                                              0),                                                \
      TUD_AUDIO_DESC_OUTPUT_TERM(GROK_UAC_SPK_OUTPUT,                                            \
                                 AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER,                            \
                                 0,                                                              \
                                 GROK_UAC_SPK_FEATURE,                                           \
                                 GROK_UAC_CLOCK,                                                 \
                                 0,                                                              \
                                 0),                                                             \
      TUD_AUDIO_DESC_INPUT_TERM(                                                                 \
          GROK_UAC_MIC_INPUT, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0, GROK_UAC_CLOCK, 1, 0, 0, 0, 0), \
      TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL(                                                   \
          GROK_UAC_MIC_FEATURE, GROK_UAC_MIC_INPUT, GROK_UAC_MUTE_CTRL, 0, 0),                   \
      TUD_AUDIO_DESC_OUTPUT_TERM(GROK_UAC_MIC_OUTPUT,                                            \
                                 AUDIO_TERM_TYPE_USB_STREAMING,                                  \
                                 0,                                                              \
                                 GROK_UAC_MIC_FEATURE,                                           \
                                 GROK_UAC_CLOCK,                                                 \
                                 0,                                                              \
                                 0),                                                             \
      TUD_AUDIO_DESC_STD_AS_INT(GROK_UAC_ITF_SPK, 0, 0, str),                                    \
      TUD_AUDIO_DESC_STD_AS_INT(GROK_UAC_ITF_SPK, 1, 2, str),                                    \
      TUD_AUDIO_DESC_CS_AS_INT(                                                                  \
          GROK_UAC_SPK_INPUT, 0, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM, 1, 0, 0),    \
      TUD_AUDIO_DESC_TYPE_I_FORMAT(2, 16),                                                       \
      TUD_AUDIO_DESC_STD_AS_ISO_EP(                                                              \
          GROK_UAC_EP_SPK, TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS, 34, interval),  \
      TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,                \
                                  0,                                                             \
                                  AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,             \
                                  0),                                                            \
      TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(GROK_UAC_EP_FB, fb_bytes, interval),                       \
      TUD_AUDIO_DESC_STD_AS_INT(GROK_UAC_ITF_MIC, 0, 0, str),                                    \
      TUD_AUDIO_DESC_STD_AS_INT(GROK_UAC_ITF_MIC, 1, 1, str),                                    \
      TUD_AUDIO_DESC_CS_AS_INT(                                                                  \
          GROK_UAC_MIC_OUTPUT, 0, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM, 1, 0, 0),   \
      TUD_AUDIO_DESC_TYPE_I_FORMAT(2, 16),                                                       \
      TUD_AUDIO_DESC_STD_AS_ISO_EP(                                                              \
          GROK_UAC_EP_MIC, TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS, 34, interval),  \
      TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,                \
                                  0,                                                             \
                                  AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,             \
                                  0)

#endif // FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_UAC_WIRE_H
