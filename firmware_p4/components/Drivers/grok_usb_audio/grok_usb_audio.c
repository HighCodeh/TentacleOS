// Experimental USB Audio Class 2 headset. No cloud/API credentials or Grok
// protocol are involved: the host chooses a normal USB audio input/output.
#include "grok_usb_audio.h"

#include <string.h>

#if HB_GROK_USB_AUDIO_ENABLED
#include <math.h>
#include "audio_i2s.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grok_uac_wire.h"
#include "host_link.h"
#include "pin_def.h"
#include "power_manager.h"
#include "sys_prio.h"
#include "tusb_desc.h"
#include "usb_msc.h"
#include "device/dcd.h"

#define LOAD(v)     __atomic_load_n(&(v), __ATOMIC_ACQUIRE)
#define STORE(v, x) __atomic_store_n(&(v), (x), __ATOMIC_RELEASE)
#define PCM_SAMPLES 80 // 5 ms; finite worker-owned transfers, never USB callbacks.

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static grok_usb_audio_view_t s_view;
static TaskHandle_t s_task;
static bool s_requested, s_ptt, s_mic_alt, s_spk_alt, s_mic_mute, s_spk_mute;
static bool s_shutdown, s_usb_owned;
static int16_t s_spk_volume; // USB dB * 256, -40 dB .. 0 dB
static uint32_t s_epoch;

static void reset_gate(void) {
  STORE(s_ptt, false);
  STORE(s_mic_alt, false);
  STORE(s_spk_alt, false);
  __atomic_add_fetch(&s_epoch, 1, __ATOMIC_ACQ_REL);
}

// This hook may run in an ISR. Only atomic scalars; no TinyUSB/I2S/RTOS calls.
void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
  (void)rhport;
  (void)in_isr;
  if (eventid == DCD_EVENT_BUS_RESET || eventid == DCD_EVENT_UNPLUGGED ||
      eventid == DCD_EVENT_SUSPEND)
    reset_gate();
}

void grok_usb_audio_get_view(grok_usb_audio_view_t *out) {
  if (!out)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_view;
  portEXIT_CRITICAL(&s_lock);
}

static void publish(const grok_usb_audio_view_t *view) {
  portENTER_CRITICAL(&s_lock);
  s_view = *view;
  // An asynchronous close always wins a worker progress publication.
  s_view.closing = s_view.is_open && !LOAD(s_requested);
  portEXIT_CRITICAL(&s_lock);
}

void grok_usb_audio_set_ptt(bool held) {
  // Releasing always closes the gate, including while setup is pending.
  if (!held) {
    STORE(s_ptt, false);
    __atomic_add_fetch(&s_epoch, 1, __ATOMIC_ACQ_REL);
    return;
  }
  grok_usb_audio_view_t v;
  grok_usb_audio_get_view(&v);
  if (LOAD(s_requested) && v.is_open && !v.opening && !v.closing && v.usb_connected)
    STORE(s_ptt, true);
}

void grok_usb_audio_close(void) {
  STORE(s_requested, false);
  reset_gate();
  portENTER_CRITICAL(&s_lock);
  s_view.closing = s_view.is_open;
  s_view.input_level = 0;
  portEXIT_CRITICAL(&s_lock);
  if (s_task)
    xTaskNotifyGive(s_task);
}

static uint8_t level(const int16_t *pcm, unsigned count) {
  unsigned peak = 0;
  for (unsigned i = 0; i < count; ++i) {
    int x = pcm[i];
    unsigned a = (unsigned)(x < 0 ? -x : x);
    if (a > peak)
      peak = a;
  }
  unsigned pct = peak * 100u / 32768u;
  return (uint8_t)(pct > 100 ? 100 : pct);
}

static bool connected(void) {
  return busb_voice_is_exposed() && usb_mux_is_native() && tud_mounted() && !tud_suspended() &&
         tud_audio_mounted();
}

static void silence_mic(void) {
  static const int16_t silence[PCM_SAMPLES] = {0};
  tu_fifo_t *ff = tud_audio_get_ep_in_ff();
  // Fill to 20 ms, not indefinitely. The host consumes one packet each ms.
  for (int i = 0; ff && i < 4 && tu_fifo_count(ff) < 640; ++i)
    if (!tud_audio_write(silence, sizeof(silence)))
      break;
}

static void shutdown_handler(void) {
  if (!LOAD(s_usb_owned))
    return;
  STORE(s_shutdown, true);
  grok_usb_audio_close();
  // Give the owner a bounded opportunity for orderly I2S/mux cleanup before
  // esp_restart stops the scheduler. Fail closed even if it was stalled.
  TickType_t start = xTaskGetTickCount();
  while (LOAD(s_usb_owned) && xTaskGetTickCount() - start < pdMS_TO_TICKS(1500))
    vTaskDelay(pdMS_TO_TICKS(10));
  if (LOAD(s_usb_owned))
    tud_disconnect();
  gpio_set_level(GPIO_AUDIO_EN_PIN, 0);
}

static void voice_worker(void *unused) {
  (void)unused;
  for (;;) {
    if (!LOAD(s_requested)) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
      continue;
    }
    grok_usb_audio_view_t v = {.is_open = true, .opening = true};
    bool prior_native = usb_mux_is_native();
    bool mic = false, speaker = false, wake_held = false, seen_host = false;
    int16_t pcm[PCM_SAMPLES];
    reset_gate();
    STORE(s_mic_mute, false);
    STORE(s_spk_mute, false);
    STORE(s_spk_volume, 0);
    usb_msc_state_t msc = usb_msc_get_state();
    if (msc != USB_MSC_IDLE && msc != USB_MSC_ERROR) {
      v.error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    STORE(s_usb_owned, true);
    v.error = busb_set_voice_exposed(true);
    if (v.error != ESP_OK || !LOAD(s_requested))
      goto cleanup;
    v.error = usb_mux_set_native(true);
    if (v.error != ESP_OK || !LOAD(s_requested))
      goto cleanup;
    v.error = host_link_cdc_init(); // Retain the normal authenticated companion.
    if (v.error != ESP_OK || !LOAD(s_requested))
      goto cleanup;
    v.error = audio_i2s_init();
    if (v.error != ESP_OK)
      goto cleanup;
    power_manager_no_sleep_acquire();
    wake_held = true;
    v.opening = false;
    publish(&v);

    while (LOAD(s_requested) && !LOAD(s_shutdown) && busb_voice_is_exposed() &&
           usb_mux_is_native()) {
      v.usb_connected = connected();
      if (!v.usb_connected) {
        reset_gate();
        // An unplug/suspend after connection ends this session. Reopening and
        // another PTT press are required; never resume capturing on reconnect.
        if (seen_host)
          break;
        publish(&v);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        continue;
      }
      seen_host = true;
      bool want_mic = LOAD(s_ptt) && LOAD(s_mic_alt) && !LOAD(s_mic_mute);
      if (!want_mic && mic) {
        audio_i2s_mic_stream_stop();
        mic = false;
        tud_audio_clear_ep_in_ff();
      }
      // Deliberate half-duplex: no speaker audio while the physical mic is on.
      if ((want_mic || !LOAD(s_spk_alt)) && speaker) {
        audio_i2s_stream_stop();
        speaker = false;
        tud_audio_clear_ep_out_ff();
      }
      if (want_mic && !mic) {
        v.error = audio_i2s_mic_stream_start(GROK_UAC_RATE);
        if (v.error != ESP_OK)
          break;
        mic = true;
        tud_audio_clear_ep_in_ff();
      }
      v.mic_active = mic && LOAD(s_ptt);
      v.speaker_active = speaker;
      v.input_level = v.output_level = 0;
      if (mic) {
        uint32_t epoch = LOAD(s_epoch);
        int n = audio_i2s_mic_stream_read(pcm, PCM_SAMPLES);
        if (n <= 0) {
          v.error = ESP_ERR_TIMEOUT;
          break;
        }
        // Drop in-flight capture after release, USB reset, close, or host mute.
        if (LOAD(s_requested) && LOAD(s_ptt) && !LOAD(s_mic_mute) && LOAD(s_mic_alt) &&
            epoch == LOAD(s_epoch) && connected()) {
          tud_audio_write(pcm, (uint16_t)(n * sizeof(*pcm)));
          v.input_level = level(pcm, (unsigned)n);
        } else {
          tud_audio_clear_ep_in_ff();
        }
        // Discard, do not replay a backlog of speech after releasing PTT.
        tud_audio_clear_ep_out_ff();
      } else {
        if (LOAD(s_mic_alt))
          silence_mic();
        if (LOAD(s_spk_alt)) {
          if (!speaker) {
            v.error = audio_i2s_stream_start(GROK_UAC_RATE);
            if (v.error != ESP_OK)
              break;
            speaker = true;
          }
          unsigned n = tud_audio_read(pcm, sizeof(pcm)) / sizeof(*pcm);
          if (n) {
            float gain = LOAD(s_spk_mute) ? 0.0f : powf(10.0f, LOAD(s_spk_volume) / 5120.0f);
            for (unsigned i = 0; i < n; ++i)
              pcm[i] = (int16_t)(pcm[i] * gain);
            v.output_level = level(pcm, n);
            if (!LOAD(s_requested) || LOAD(s_ptt) || !connected())
              continue;
            if (audio_i2s_stream_write(pcm, (int)n) != (int)n) {
              v.error = ESP_ERR_TIMEOUT;
              break;
            }
          }
          v.speaker_active = speaker;
        }
        // Worker wakes frequently even if the host advertises silence/no data.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
      }
      publish(&v);
    }

cleanup:
    STORE(s_requested, false);
    reset_gate();
    v.closing = true;
    v.opening = false;
    v.mic_active = v.speaker_active = false;
    v.input_level = v.output_level = 0;
    publish(&v);
    // Channel lifecycle is owned entirely by this worker, including failures.
    if (mic)
      audio_i2s_mic_stream_stop();
    if (speaker)
      audio_i2s_stream_stop();
    if (LOAD(s_usb_owned)) {
      (void)busb_set_voice_exposed(false);
      // An explicitly selected storage session wins over restoring our mux.
      if (!busb_msc_is_exposed())
        (void)usb_mux_set_native(prior_native);
      STORE(s_usb_owned, false);
    }
    if (wake_held)
      power_manager_no_sleep_release();
    esp_err_t error = v.error;
    memset(&v, 0, sizeof(v));
    v.error = error;
    publish(&v);
  }
}

esp_err_t grok_usb_audio_open(void) {
  // Public lifecycle is called by the UI task; view/state are cross-core safe.
  portENTER_CRITICAL(&s_lock);
  if (s_view.is_open || LOAD(s_shutdown)) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  memset(&s_view, 0, sizeof(s_view));
  s_view.is_open = s_view.opening = true;
  STORE(s_requested, true);
  STORE(s_ptt, false);
  portEXIT_CRITICAL(&s_lock);
  if (!s_task) {
    esp_err_t e = esp_register_shutdown_handler(shutdown_handler);
    if (e == ESP_OK && xTaskCreatePinnedToCore(voice_worker,
                                               "grok_usb_audio",
                                               6144,
                                               NULL,
                                               SYS_PRIO_SERVICE_HI,
                                               &s_task,
                                               SYS_CORE_RADIO) != pdPASS) {
      esp_unregister_shutdown_handler(shutdown_handler);
      e = ESP_ERR_NO_MEM;
    }
    if (e != ESP_OK) {
      STORE(s_requested, false);
      grok_usb_audio_view_t v = {.error = e};
      publish(&v);
      return e;
    }
  }
  xTaskNotifyGive(s_task);
  return ESP_OK;
}

// USB control callbacks only parse/copy scalars; the worker owns all audio I/O.
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *r) {
  (void)rhport;
  unsigned itf = tu_le16toh(r->wIndex), alt = tu_le16toh(r->wValue);
  if (alt > 1 || (itf != GROK_UAC_ITF_MIC && itf != GROK_UAC_ITF_SPK))
    return false;
  if (itf == GROK_UAC_ITF_MIC)
    STORE(s_mic_alt, alt == 1);
  else
    STORE(s_spk_alt, alt == 1);
  return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *r) {
  (void)rhport;
  unsigned itf = tu_le16toh(r->wIndex);
  if (itf == GROK_UAC_ITF_MIC) {
    STORE(s_mic_alt, false);
    STORE(s_ptt, false);
    __atomic_add_fetch(&s_epoch, 1, __ATOMIC_ACQ_REL);
  } else if (itf == GROK_UAC_ITF_SPK)
    STORE(s_spk_alt, false);
  return true;
}

bool tud_audio_get_req_entity_cb(uint8_t port, tusb_control_request_t const *r) {
  unsigned entity = tu_le16toh(r->wIndex) >> 8;
  unsigned selector = tu_le16toh(r->wValue) >> 8;
  unsigned channel = tu_le16toh(r->wValue) & 255;
  if ((tu_le16toh(r->wIndex) & 255) != GROK_UAC_ITF_AC || channel)
    return false;
  if (entity == GROK_UAC_CLOCK) {
    if (selector == AUDIO_CS_CTRL_SAM_FREQ && r->bRequest == AUDIO_CS_REQ_CUR) {
      uint32_t rate = tu_htole32(GROK_UAC_RATE);
      return tud_audio_buffer_and_schedule_control_xfer(port, r, &rate, sizeof(rate));
    }
    if (selector == AUDIO_CS_CTRL_SAM_FREQ && r->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_4_n_t(1) range = {
          .wNumSubRanges = tu_htole16(1),
          .subrange = {
              {.bMin = tu_htole32(GROK_UAC_RATE), .bMax = tu_htole32(GROK_UAC_RATE), .bRes = 0}}};
      return tud_audio_buffer_and_schedule_control_xfer(port, r, &range, sizeof(range));
    }
    if (selector == AUDIO_CS_CTRL_CLK_VALID && r->bRequest == AUDIO_CS_REQ_CUR) {
      uint8_t valid = 1;
      return tud_audio_buffer_and_schedule_control_xfer(port, r, &valid, 1);
    }
  }
  if (entity != GROK_UAC_SPK_FEATURE && entity != GROK_UAC_MIC_FEATURE)
    return false;
  if (selector == AUDIO_FU_CTRL_MUTE && r->bRequest == AUDIO_CS_REQ_CUR) {
    uint8_t muted = entity == GROK_UAC_MIC_FEATURE ? LOAD(s_mic_mute) : LOAD(s_spk_mute);
    return tud_audio_buffer_and_schedule_control_xfer(port, r, &muted, 1);
  }
  if (entity == GROK_UAC_SPK_FEATURE && selector == AUDIO_FU_CTRL_VOLUME) {
    if (r->bRequest == AUDIO_CS_REQ_CUR) {
      int16_t volume = (int16_t)tu_htole16((uint16_t)LOAD(s_spk_volume));
      return tud_audio_buffer_and_schedule_control_xfer(port, r, &volume, 2);
    }
    if (r->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_2_n_t(1) range = {
          .wNumSubRanges = tu_htole16(1),
          .subrange = {
              {.bMin = (int16_t)tu_htole16((uint16_t)-10240), .bMax = 0, .bRes = tu_htole16(256)}}};
      return tud_audio_buffer_and_schedule_control_xfer(port, r, &range, sizeof(range));
    }
  }
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t port, tusb_control_request_t const *r, uint8_t *data) {
  (void)port;
  unsigned entity = tu_le16toh(r->wIndex) >> 8;
  unsigned selector = tu_le16toh(r->wValue) >> 8;
  unsigned channel = tu_le16toh(r->wValue) & 255;
  unsigned len = tu_le16toh(r->wLength);
  if (!data || (tu_le16toh(r->wIndex) & 255) != GROK_UAC_ITF_AC || channel ||
      r->bRequest != AUDIO_CS_REQ_CUR)
    return false;
  if (entity == GROK_UAC_CLOCK && selector == AUDIO_CS_CTRL_SAM_FREQ && len == 4)
    return tu_unaligned_read32(data) == tu_htole32(GROK_UAC_RATE);
  if (selector == AUDIO_FU_CTRL_MUTE && len == 1 && data[0] <= 1) {
    if (entity == GROK_UAC_MIC_FEATURE) {
      STORE(s_mic_mute, data[0] != 0);
      if (data[0])
        grok_usb_audio_set_ptt(false);
      return true;
    }
    if (entity == GROK_UAC_SPK_FEATURE) {
      STORE(s_spk_mute, data[0] != 0);
      return true;
    }
  }
  if (entity == GROK_UAC_SPK_FEATURE && selector == AUDIO_FU_CTRL_VOLUME && len == 2) {
    int16_t db = (int16_t)tu_le16toh(tu_unaligned_read16(data));
    if (db < -10240 || db > 0)
      return false;
    STORE(s_spk_volume, db);
    return true;
  }
  return false;
}

void tud_audio_feedback_params_cb(uint8_t func, uint8_t alt, audio_feedback_params_t *p) {
  (void)func;
  (void)alt;
  p->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
  p->sample_freq = GROK_UAC_RATE;
}

bool tud_audio_feedback_format_correction_cb(uint8_t func) {
  (void)func;
  // Standards-compliant/macOS FS 3-byte 10.14 feedback; HS remains 16.16.
  return true;
}

#else
esp_err_t grok_usb_audio_open(void) {
  return ESP_ERR_NOT_SUPPORTED;
}
void grok_usb_audio_close(void) {}
void grok_usb_audio_set_ptt(bool held) {
  (void)held;
}
void grok_usb_audio_get_view(grok_usb_audio_view_t *out) {
  if (out)
    memset(out, 0, sizeof(*out));
}
#endif
