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

#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Bring up I2S_NUM_0 on the MAX98357 amp pins and start the audio
 *        playback task. Idempotent - safe to call multiple times.
 */
esp_err_t audio_i2s_init(void);

/**
 * @brief Global output volume (0..100), applied on top of each sound's own
 *        amplitude. Perceptual (square-law) curve so the slider feels linear.
 *        Hardware gain on the MAX98357A is fixed, so this is digital scaling.
 */
void audio_i2s_set_volume(uint8_t pct);

/**
 * @brief One note for audio_i2s_play_song(). freq_hz == 0 means a rest (silence).
 */
typedef struct {
  uint16_t freq_hz; ///< Tone frequency in Hz (0 = rest/silence).
  uint16_t dur_ms;  ///< Note duration in milliseconds.
} audio_note_t;

/**
 * @brief Play a melody (blocking, gapless): opens ONE I2S channel, renders all
 *        notes back-to-back with per-note anti-click attack/release envelopes
 *        and a continuous phase accumulator, then closes. Far cleaner than
 *        calling play_tone per note (no per-note channel teardown clicks/gaps).
 */
esp_err_t audio_i2s_play_song(const audio_note_t *notes, int count, float amp);

/**
 * @brief Per-note hook for audio_i2s_play_song_cb(). Called once, in the
 *        CALLER's (worker) thread, just before each note is rendered, with the
 *        note index, total count and that note's frequency. Return false to
 *        cancel playback (cooperative STOP). MUST be trivial - only touch
 *        volatile scalars; NEVER call any lv_* function from here.
 */
typedef bool (*audio_song_progress_cb_t)(int note_index,
                                         int note_count,
                                         uint16_t freq_hz,
                                         void *ctx);

/**
 * @brief Like audio_i2s_play_song() (gapless, single open channel, anti-click
 *        envelopes) but reports per-note progress and supports cooperative
 *        cancel: @p cb is invoked before each note; returning false stops after
 *        the current note (a short tail-silence is still flushed). Pass cb=NULL
 *        for plain playback.
 */
esp_err_t audio_i2s_play_song_cb(
    const audio_note_t *notes, int count, float amp, audio_song_progress_cb_t cb, void *ctx);

/**
 * @brief Play a single sine tone (blocking) at 44.1 kHz mono.
 *
 * @param freq_hz  Tone frequency in Hz.
 * @param dur_ms   Duration in milliseconds.
 * @param amp      Amplitude in [0..1].
 * @return ESP_OK on success, otherwise an esp_err_t error code.
 */
esp_err_t audio_i2s_play_tone(float freq_hz, int dur_ms, float amp);

/**
 * @brief Play a raw 16-bit mono PCM buffer (blocking) at @p sample_rate.
 *
 * @param pcm          Pointer to the 16-bit mono PCM samples.
 * @param n_samples    Number of samples in @p pcm.
 * @param sample_rate  Playback sample rate in Hz.
 * @return ESP_OK on success, otherwise an esp_err_t error code.
 */
esp_err_t audio_i2s_play_pcm(const int16_t *pcm, size_t n_samples, uint32_t sample_rate);

/**
 * @brief Per-chunk live-level callback for audio_i2s_mic_record(). @p peak is
 *        the max |sample| (0..32767) of the chunk just captured; drives a live
 *        VU meter. Runs in the recording task's context - keep it trivial.
 */
typedef void (*audio_mic_level_cb_t)(int peak, int rms, void *ctx);

/**
 * @brief Capture @p max_samples of 16-bit mono PCM from the PDM mic (blocking).
 *        Opens an I2S_NUM_0 PDM-RX channel, lets the HP filter settle, fills
 *        @p out, then closes. *out_captured gets the real sample count. If
 *        @p cb is non-NULL it is invoked after each chunk with that chunk's
 *        peak level for a live meter.
 */
esp_err_t audio_i2s_mic_record(int16_t *out,
                               size_t max_samples,
                               uint32_t sample_rate,
                               size_t *out_captured,
                               audio_mic_level_cb_t cb,
                               void *ctx);

/**
 * @brief Open a continuous PDM-RX stream at @p sample_rate. Idempotent.
 *
 * @param sample_rate  Capture sample rate in Hz.
 * @return ESP_OK on success, otherwise an esp_err_t error code.
 */
esp_err_t audio_i2s_mic_stream_start(uint32_t sample_rate);

/**
 * @brief Read up to @p max_samples 16-bit mono samples from the open stream.
 * @return Number of samples read (0 on error/timeout/not-started).
 */
int audio_i2s_mic_stream_read(int16_t *buf, int max_samples);

/**
 * @brief Stop and free the streaming mic channel. Safe to call when not started.
 */
void audio_i2s_mic_stream_stop(void);

/**
 * @brief Begin a continuous 16-bit mono TX stream at @p sample_rate (e.g. WAV
 *        playback). Reuses the shared TX channel (reconfigures its clock) and
 *        suppresses UI chimes/clicks for the duration so they don't fight the
 *        stream. Follow with audio_i2s_stream_write() then audio_i2s_stream_stop().
 *
 * @param sample_rate  Stream sample rate in Hz.
 * @return ESP_OK on success, otherwise an esp_err_t error code.
 */
esp_err_t audio_i2s_stream_start(uint32_t sample_rate);

/**
 * @brief Write 16-bit mono samples to the open TX stream (blocking; paces to
 *        real time via the I2S DMA). Global volume is applied. No-op if the
 *        stream is not started.
 *
 * @param pcm        16-bit mono samples.
 * @param n_samples  Number of samples.
 * @return Samples written, or -1 if the stream is not active.
 */
int audio_i2s_stream_write(const int16_t *pcm, int n_samples);

/**
 * @brief Stop the TX stream, restore the default clock and re-enable UI sounds.
 *        Safe to call when not started.
 */
void audio_i2s_stream_stop(void);

/**
 * @brief Queue a 3-note "boot chime" (C5 -> E5 -> G5, ~200 ms each).
 *        Non-blocking; dropped if the queue is full.
 */
void audio_play_chime(void);

/**
 * @brief Queue a short click tone (~30 ms, ~2 kHz). Non-blocking;
 *        dropped if the queue already has 4 pending entries so rapid
 *        button mashing never piles up.
 */
void audio_click(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_I2S_H
