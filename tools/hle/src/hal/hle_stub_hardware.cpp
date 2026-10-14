// Stand-ins for P4 components the HLE CMakeLists excludes because they need
// the real SoC. Each reports that the hardware is absent instead of pretending
// to work, so the screens that use them take their normal error paths.

extern "C" {
#include "audio_i2s.h"
#include "doom_highboy.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb_msc.h"
}

static const char *TAG = "HLE_HW";

extern "C" {

// ── Audio (I2S amp + PDM mic) ─────────────────────────────────────────────────

esp_err_t audio_i2s_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void audio_i2s_set_volume(uint8_t pct) { (void)pct; }
esp_err_t audio_i2s_play_song(const audio_note_t *notes, int count, float amp) {
    (void)notes; (void)count; (void)amp;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t audio_i2s_play_song_cb(const audio_note_t *notes, int count, float amp,
                                 audio_song_progress_cb_t cb, void *ctx) {
    (void)notes; (void)count; (void)amp; (void)cb; (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t audio_i2s_play_tone(float freq_hz, int dur_ms, float amp) {
    (void)freq_hz; (void)dur_ms; (void)amp;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t audio_i2s_play_pcm(const int16_t *pcm, size_t n_samples, uint32_t sample_rate) {
    (void)pcm; (void)n_samples; (void)sample_rate;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t audio_i2s_mic_record(int16_t *out, size_t max_samples, uint32_t sample_rate,
                               size_t *out_captured, audio_mic_level_cb_t cb, void *ctx) {
    (void)out; (void)max_samples; (void)sample_rate; (void)cb; (void)ctx;
    if (out_captured != nullptr) *out_captured = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t audio_i2s_mic_stream_start(uint32_t sample_rate) {
    (void)sample_rate;
    return ESP_ERR_NOT_SUPPORTED;
}
int audio_i2s_mic_stream_read(int16_t *buf, int max_samples) {
    (void)buf; (void)max_samples;
    return 0;
}
void audio_i2s_mic_stream_stop(void) {}
esp_err_t audio_i2s_stream_start(uint32_t sample_rate) {
    (void)sample_rate;
    return ESP_ERR_NOT_SUPPORTED;
}
int audio_i2s_stream_write(const int16_t *pcm, int n_samples) {
    (void)pcm; (void)n_samples;
    return 0;
}
void audio_i2s_stream_stop(void) {}
void audio_play_chime(void) {}
void audio_click(void) {}

// ── USB mass storage (TinyUSB) ────────────────────────────────────────────────

static usb_msc_state_t s_msc_state = USB_MSC_IDLE;

usb_msc_state_t usb_msc_get_state(void) { return s_msc_state; }
bool usb_msc_host_connected(void) { return false; }
void usb_msc_enter(void) { s_msc_state = USB_MSC_ERROR; }
void usb_msc_exit(void) { s_msc_state = USB_MSC_IDLE; }

// ── .hb apps: the loader maps RISC-V code, which a host process cannot run ────

esp_err_t tos_elf_load(const uint8_t *elf, size_t elf_len, void *out) {
    (void)elf; (void)elf_len; (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}
int tos_elf_call(const void *img, const void *api) {
    (void)img; (void)api;
    return -1;
}
void tos_elf_unload(void *img) { (void)img; }

// ── DOOM ──────────────────────────────────────────────────────────────────────

void highboy_doom_start(void (*render_beat_kick)(void)) {
    (void)render_beat_kick;
    ESP_LOGW(TAG, "DOOM is not available in HLE");
}

} // extern "C"
