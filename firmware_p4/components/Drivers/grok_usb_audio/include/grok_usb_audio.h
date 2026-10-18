#ifndef FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_USB_AUDIO_H
#define FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_USB_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  // True from an accepted open request until worker cleanup completes.
  bool is_open;
  bool opening;
  bool closing;
  bool usb_connected; // USB audio enumeration only, NOT a Grok Bot connection.
  bool mic_active;    // Physical capture enabled by a fresh held PTT request.
  bool speaker_active;
  uint8_t input_level;  // 0..100
  uint8_t output_level; // 0..100
  esp_err_t error;      // ESP_OK, or a latched setup/stream error until next open.
} grok_usb_audio_view_t;

// Explicit user confirmation is required before open. Never call on UI init.
// These are nonblocking requests; only the worker changes USB/I2S lifecycle.
// Open fails with ESP_ERR_NOT_SUPPORTED in builds without experimental audio.
esp_err_t grok_usb_audio_open(void);
void grok_usb_audio_close(void);
void grok_usb_audio_set_ptt(bool held);
void grok_usb_audio_get_view(grok_usb_audio_view_t *out);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_P4_COMPONENTS_DRIVERS_GROK_USB_AUDIO_INCLUDE_GROK_USB_AUDIO_H
