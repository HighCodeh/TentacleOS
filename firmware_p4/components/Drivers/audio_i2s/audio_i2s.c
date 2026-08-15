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

// NS4168 amp over I2S_NUM_0, plus the PDM mic on the same port's RX channel.
//
// Design notes, because two of these were learned the hard way:
//
//  1. ONE persistent TX channel. I2S_NUM_0 has exactly one TX channel, so a
//     second i2s_new_channel() on it always fails. It is created once here and
//     never deleted; playback only enables/disables it (cheap - no DMA realloc)
//     and reconfigures its clock when the sample rate changes. Creating and
//     deleting a channel per sound churned ~13 KB of internal DMA RAM and an
//     interrupt handler on every coverflow tick, which fragmented the heap LVGL
//     draws from and made the UI stutter.
//
//  2. NOTHING here waits forever. play_tone/_song/_pcm are called straight from
//     the LVGL thread, so every lock has a finite timeout and drops the sound
//     rather than stalling the UI. A missed chirp beats a frozen screen.

#include "audio_i2s.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "pin_def.h"

static const char *TAG = "AUDIO_I2S";

#define SAMPLE_RATE_HZ   44100
#define CHUNK_SAMPLES    1024
#define MAX_TONES_PER_FX 4
#define QUEUE_DEPTH      4

#define AUDIO_TASK_STACK_SIZE 4096
#define AUDIO_TASK_PRIORITY   SYS_PRIO_SERVICE_LO
// NOT SYS_CORE_UI: the LVGL renderer owns that core at SYS_PRIO_RENDER (6),
// above this task, and a full-frame redraw holds it long enough to starve the
// I2S DMA. Rendering audio on the radio core keeps the sample feed steady.
#define AUDIO_TASK_CORE SYS_CORE_RADIO

// DMA depth, allocated once at init and kept for the life of the firmware.
// A frame is one sample in ALL slots, so std 16-bit stereo = 4 bytes/frame:
// 8 x 256 = 2048 frames ~= 46 ms, ~8 KB of internal RAM. Deeper than the IDF
// default (~32 ms) for underrun slack, but not so deep that the queue latency
// between writing a chirp and hearing it becomes noticeable.
#define AUDIO_DMA_DESC_NUM  8
#define AUDIO_DMA_FRAME_NUM 256

// Ceiling on how long a caller waits for the shared TX channel. Bounds the
// worst-case UI stall; past this the sound is dropped.
#define TX_LOCK_WAIT_MS 250
// Queued UI effects are disposable: never let one wait behind a melody.
#define FX_TX_WAIT_MS 0

#define ENV_ATTACK_MS  4
#define ENV_RELEASE_MS 8

typedef struct {
  float freq_hz;
  uint16_t dur_ms;
  float amp;
} tone_t;

typedef struct {
  tone_t tones[MAX_TONES_PER_FX];
  uint8_t count;
} fx_t;

// The single TX channel and its current clock rate. Both are owned by s_tx_mux.
static i2s_chan_handle_t s_tx = NULL;
static uint32_t s_tx_rate = 0;
static bool s_tx_enabled = false;

static SemaphoreHandle_t s_tx_mux = NULL;
static QueueHandle_t s_fx_q = NULL;
static int16_t *s_chunk_buf = NULL;
static bool s_ready = false;
static volatile bool s_streaming = false;

static i2s_chan_handle_t s_rx_stream = NULL;

static float s_play_vol = 1.0f;

void audio_i2s_set_volume(uint8_t pct) {
  if (pct > 100)
    pct = 100;
  float n = pct / 100.0f;
  s_play_vol = n * n;
}

static bool tx_lock(TickType_t wait) {
  if (s_tx_mux == NULL)
    return false;
  return xSemaphoreTake(s_tx_mux, wait) == pdTRUE;
}

static void tx_unlock(void) {
  if (s_tx_mux != NULL)
    (void)xSemaphoreGive(s_tx_mux);
}

/**
 * @brief Take the NS4168 out of shutdown (CTRL/EN high). Idempotent.
 */
static void amp_enable(void) {
  gpio_config_t en_cfg = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ULL << GPIO_AUDIO_EN_PIN,
      .intr_type = GPIO_INTR_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
  };
  gpio_config(&en_cfg);
  gpio_set_level(GPIO_AUDIO_EN_PIN, 1);
}

// ---------------------------------------------------------------------------
// Shared TX channel. All of these require the caller to hold s_tx_mux.
// ---------------------------------------------------------------------------

/**
 * @brief Push enough silence to flush everything already queued out of the DMA.
 *
 * i2s_channel_write() only COPIES into the DMA queue - it returns long before
 * the samples reach the pin. Writing a full DMA's worth of zeros blocks until
 * the queue has drained that much, which means the real audio ahead of it has
 * actually been transmitted. Required before any disable, because
 * i2s_channel_disable() does xQueueReset() on the TX queue and throws away
 * whatever had not gone out yet.
 */
static void tx_drain(void) {
  const int dma_samples = AUDIO_DMA_DESC_NUM * AUDIO_DMA_FRAME_NUM;
  memset(s_chunk_buf, 0, CHUNK_SAMPLES * sizeof(int16_t));
  for (int p = 0; p < dma_samples;) {
    int n = (dma_samples - p > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (dma_samples - p);
    size_t w = 0;
    if (i2s_channel_write(s_tx, s_chunk_buf, n * sizeof(int16_t), &w, pdMS_TO_TICKS(300)) != ESP_OK)
      return;
    p += n;
  }
}

/**
 * @brief Point the TX channel at @p rate, reconfiguring its clock only when the
 *        rate actually changes.
 *
 * The channel has to be disabled to reconfigure, so this drains first - any
 * sound still in flight would otherwise be cut off mid-way.
 */
static esp_err_t tx_set_rate(uint32_t rate) {
  if (s_tx_rate == rate)
    return ESP_OK;

  if (s_tx_enabled) {
    tx_drain();
    esp_err_t stop = i2s_channel_disable(s_tx);
    if (stop != ESP_OK)
      return stop;
    s_tx_enabled = false;
  }

  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  esp_err_t err = i2s_channel_reconfig_std_clock(s_tx, &clk);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "reconfig clock to %lu Hz: %s", (unsigned long)rate, esp_err_to_name(err));
    return err;
  }
  s_tx_rate = rate;
  return ESP_OK;
}

/** @brief Set the rate and make sure the channel is running. */
static esp_err_t tx_begin(uint32_t rate) {
  esp_err_t err = tx_set_rate(rate);
  if (err != ESP_OK)
    return err;
  if (!s_tx_enabled) {
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "channel_enable: %s", esp_err_to_name(err));
      return err;
    }
    s_tx_enabled = true;
  }
  return ESP_OK;
}

/**
 * @brief End of a sound: wait for it to actually reach the amp.
 *
 * Deliberately does NOT disable the channel - disabling is what made sounds
 * vanish, since i2s_channel_disable() resets the TX queue and a chirp shorter
 * than the DMA depth was discarded before a single sample got out. The channel
 * stays enabled and auto_clear feeds it zeros while idle.
 *
 * Draining here is what makes the mutex mean "the speaker is busy" instead of
 * merely "someone is still copying bytes". Without it a caller released the
 * lock with up to a full DMA of audio still queued, and the next sound was
 * written straight behind it - which is what running one sound into the next
 * sounded like.
 */
static void tx_end(void) {
  tx_drain();
}

/**
 * @brief Write exactly @p n_samples, looping over short writes.
 *
 * i2s_channel_write() can return having written LESS than asked (it stops at
 * the timeout). Treating that as a full write silently drops samples, which is
 * audible as a click or a torn note. Returns the samples actually written.
 */
static int write_all(const int16_t *buf, int n_samples) {
  int done = 0;
  while (done < n_samples) {
    size_t w = 0;
    esp_err_t err = i2s_channel_write(
        s_tx, &buf[done], (n_samples - done) * sizeof(int16_t), &w, pdMS_TO_TICKS(500));
    done += (int)(w / sizeof(int16_t));
    if (err != ESP_OK || w == 0)
      break; // stalled or disabled underneath us: give up rather than spin
  }
  return done;
}

static void write_silence(int rate, int dur_ms) {
  const int total = (rate * dur_ms) / 1000;
  memset(s_chunk_buf, 0, CHUNK_SAMPLES * sizeof(int16_t));
  for (int p = 0; p < total;) {
    int n = (total - p > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - p);
    if (write_all(s_chunk_buf, n) != n)
      return;
    p += n;
  }
}

static void render_note(float freq_hz, int dur_ms, float amp) {
  const int total = (SAMPLE_RATE_HZ * dur_ms) / 1000;
  if (total <= 0)
    return;
  const int atk = (SAMPLE_RATE_HZ * ENV_ATTACK_MS) / 1000;
  const int rel = (SAMPLE_RATE_HZ * ENV_RELEASE_MS) / 1000;
  const float vol = amp * s_play_vol;
  const float dphase = 2.0f * 3.14159265f * freq_hz / (float)SAMPLE_RATE_HZ;
  float phase = 0.0f;

  for (int p = 0; p < total;) {
    int n = (total - p > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - p);
    for (int i = 0; i < n; i++) {
      if (freq_hz <= 0.0f) {
        s_chunk_buf[i] = 0;
        continue;
      }
      int idx = p + i;
      float env = 1.0f;
      if (idx < atk)
        env = 0.5f * (1.0f - cosf(3.14159265f * idx / atk));
      else if (idx >= total - rel)
        env = 0.5f * (1.0f - cosf(3.14159265f * (total - idx) / rel));
      int v = (int)(sinf(phase) * vol * env * 32760.0f);
      phase += dphase;
      if (phase > 6.2831853f)
        phase -= 6.2831853f;
      if (v > 32767)
        v = 32767;
      else if (v < -32768)
        v = -32768;
      s_chunk_buf[i] = (int16_t)v;
    }
    if (write_all(s_chunk_buf, n) != n)
      return;
    p += n;
  }
}

/** @brief Render a note list on the already-locked, already-begun channel. */
static void play_notes_locked(
    const audio_note_t *notes, int count, float amp, audio_song_progress_cb_t cb, void *ctx) {
  write_silence(SAMPLE_RATE_HZ, 8);
  for (int i = 0; i < count; i++) {
    if (cb != NULL && !cb(i, count, notes[i].freq_hz, ctx))
      break;
    render_note((float)notes[i].freq_hz, notes[i].dur_ms, amp);
  }
  write_silence(SAMPLE_RATE_HZ, 8);
}

// ---------------------------------------------------------------------------

static void audio_task(void *arg) {
  (void)arg;
  fx_t fx;
  while (true) {
    if (xQueueReceive(s_fx_q, &fx, portMAX_DELAY) != pdTRUE)
      continue;
    if (s_streaming)
      continue;
    if (!tx_lock(pdMS_TO_TICKS(FX_TX_WAIT_MS)))
      continue; // busy with a melody or a stream: drop this effect
    if (!s_streaming && tx_begin(SAMPLE_RATE_HZ) == ESP_OK) {
      write_silence(SAMPLE_RATE_HZ, 8);
      for (int i = 0; i < fx.count; i++)
        render_note(fx.tones[i].freq_hz, fx.tones[i].dur_ms, fx.tones[i].amp);
      write_silence(SAMPLE_RATE_HZ, 8);
      tx_end();
    }
    tx_unlock();
  }
}

esp_err_t audio_i2s_init(void) {
  if (s_ready)
    return ESP_OK;

  amp_enable();

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = AUDIO_DMA_DESC_NUM;
  chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
  // On underrun send zeros instead of re-sending the last DMA buffer. The
  // default (false) loops the stale buffer, which is what a starved channel
  // sounds like: a buzzing, torn repeat of the last few milliseconds.
  chan_cfg.auto_clear = true;

  esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
    return err;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = GPIO_AUDIO_BCLK_PIN,
              .ws = GPIO_AUDIO_LRCLK_PIN,
              .dout = GPIO_AUDIO_DIN_PIN,
              .din = I2S_GPIO_UNUSED,
          },
  };
  err = i2s_channel_init_std_mode(s_tx, &std_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "init_std_mode: %s", esp_err_to_name(err));
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return err;
  }
  s_tx_rate = SAMPLE_RATE_HZ;
  s_tx_enabled = false; // enabled on demand by tx_begin()

  s_chunk_buf = heap_caps_malloc(CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_DMA);
  if (s_chunk_buf == NULL) {
    ESP_LOGE(TAG, "chunk buf alloc failed");
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_tx_mux = xSemaphoreCreateMutex();
  if (s_tx_mux == NULL) {
    free(s_chunk_buf);
    s_chunk_buf = NULL;
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_fx_q = xQueueCreate(QUEUE_DEPTH, sizeof(fx_t));
  if (s_fx_q == NULL) {
    vSemaphoreDelete(s_tx_mux);
    s_tx_mux = NULL;
    free(s_chunk_buf);
    s_chunk_buf = NULL;
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreatePinnedToCore(audio_task,
                              "audio_i2s",
                              AUDIO_TASK_STACK_SIZE,
                              NULL,
                              AUDIO_TASK_PRIORITY,
                              NULL,
                              AUDIO_TASK_CORE) != pdPASS) {
    vQueueDelete(s_fx_q);
    s_fx_q = NULL;
    vSemaphoreDelete(s_tx_mux);
    s_tx_mux = NULL;
    free(s_chunk_buf);
    s_chunk_buf = NULL;
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_ready = true;
  ESP_LOGI(TAG,
           "audio I2S ready (EN=%d BCLK=%d WS=%d DOUT=%d @ %d Hz)",
           GPIO_AUDIO_EN_PIN,
           GPIO_AUDIO_BCLK_PIN,
           GPIO_AUDIO_LRCLK_PIN,
           GPIO_AUDIO_DIN_PIN,
           SAMPLE_RATE_HZ);
  return ESP_OK;
}

void audio_play_chime(void) {
  if (!s_ready)
    return;
  fx_t fx = {
      .count = 3,
      .tones =
          {
              {523.25f, 130, 0.35f},
              {659.25f, 130, 0.35f},
              {783.99f, 180, 0.35f},
          },
  };
  (void)xQueueSend(s_fx_q, &fx, 0);
}

void audio_click(void) {
  if (!s_ready)
    return;
  fx_t fx = {
      .count = 1,
      .tones = {{2000.0f, 30, 0.18f}},
  };
  (void)xQueueSend(s_fx_q, &fx, 0);
}

esp_err_t audio_i2s_play_tone(float freq_hz, int dur_ms, float amp) {
  if (!s_ready || s_streaming)
    return ESP_OK;
  if (!tx_lock(pdMS_TO_TICKS(TX_LOCK_WAIT_MS)))
    return ESP_ERR_TIMEOUT; // busy: drop the sound, never stall the caller

  esp_err_t err = tx_begin(SAMPLE_RATE_HZ);
  if (err == ESP_OK) {
    write_silence(SAMPLE_RATE_HZ, 8);
    render_note(freq_hz, dur_ms, amp);
    write_silence(SAMPLE_RATE_HZ, 8);
    tx_end();
  }
  tx_unlock();
  return err;
}

esp_err_t audio_i2s_play_song_cb(
    const audio_note_t *notes, int count, float amp, audio_song_progress_cb_t cb, void *ctx) {
  if (notes == NULL || count <= 0)
    return ESP_ERR_INVALID_ARG;
  if (!s_ready || s_streaming)
    return ESP_OK;
  if (!tx_lock(pdMS_TO_TICKS(TX_LOCK_WAIT_MS)))
    return ESP_ERR_TIMEOUT;

  esp_err_t err = tx_begin(SAMPLE_RATE_HZ);
  if (err == ESP_OK) {
    play_notes_locked(notes, count, amp, cb, ctx);
    tx_end();
  }
  tx_unlock();
  return err;
}

esp_err_t audio_i2s_play_song(const audio_note_t *notes, int count, float amp) {
  return audio_i2s_play_song_cb(notes, count, amp, NULL, NULL);
}

esp_err_t audio_i2s_play_pcm(const int16_t *pcm, size_t n_samples, uint32_t sample_rate) {
  if (pcm == NULL || n_samples == 0)
    return ESP_ERR_INVALID_ARG;
  if (!s_ready || s_streaming)
    return ESP_OK;
  if (!tx_lock(pdMS_TO_TICKS(TX_LOCK_WAIT_MS)))
    return ESP_ERR_TIMEOUT;

  esp_err_t err = tx_begin(sample_rate);
  if (err == ESP_OK) {
    size_t off = 0;
    while (off < n_samples) {
      size_t chunk = (n_samples - off > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (n_samples - off);
      int wrote = write_all(&pcm[off], (int)chunk);
      if (wrote != (int)chunk) {
        ESP_LOGE(TAG, "play_pcm: short write @ %u (%d/%u)", (unsigned)off, wrote, (unsigned)chunk);
        err = ESP_FAIL;
        break;
      }
      off += chunk;
    }
    write_silence((int)sample_rate, 20); // tail so the amp settles on silence
    tx_end();
  }
  tx_unlock();
  return err;
}

esp_err_t audio_i2s_stream_start(uint32_t sample_rate) {
  if (!s_ready)
    return ESP_ERR_INVALID_STATE;
  if (s_streaming)
    audio_i2s_stream_stop();

  // Held only long enough to claim the channel; the stream then owns it via
  // s_streaming until stream_stop(), which every one-shot path checks.
  if (!tx_lock(pdMS_TO_TICKS(TX_LOCK_WAIT_MS)))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = tx_begin(sample_rate);
  if (err == ESP_OK)
    s_streaming = true;
  else
    ESP_LOGE(TAG, "stream_start: %s", esp_err_to_name(err));
  tx_unlock();
  return err;
}

int audio_i2s_stream_write(const int16_t *pcm, int n_samples) {
  if (!s_streaming || pcm == NULL || n_samples <= 0)
    return -1;
  if (!tx_lock(pdMS_TO_TICKS(TX_LOCK_WAIT_MS)))
    return 0;

  int done = 0;
  while (done < n_samples) {
    int n = (n_samples - done > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (n_samples - done);
    for (int i = 0; i < n; i++) {
      int v = (int)(pcm[done + i] * s_play_vol);
      if (v > 32767)
        v = 32767;
      else if (v < -32768)
        v = -32768;
      s_chunk_buf[i] = (int16_t)v;
    }
    int wrote = write_all(s_chunk_buf, n);
    done += wrote;
    if (wrote != n)
      break; // channel stalled mid-stream
  }
  tx_unlock();
  return done;
}

void audio_i2s_stream_stop(void) {
  if (!s_streaming)
    return;
  s_streaming = false;
  if (!tx_lock(pdMS_TO_TICKS(TX_LOCK_WAIT_MS)))
    return;
  // Drain rather than a short tail: the last chunk of the track is still
  // sitting in the DMA queue at this point and would be lost on the next
  // rate change (which disables the channel).
  tx_drain();
  tx_unlock();
}

// ---------------------------------------------------------------------------
// PDM mic. Independent RX channel on the same port - unaffected by the TX side.
// ---------------------------------------------------------------------------

static void mic_pdm_config(i2s_pdm_rx_config_t *cfg, uint32_t sample_rate) {
  *cfg = (i2s_pdm_rx_config_t){
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .clk = GPIO_MIC_PDM_CLK_PIN,
              .din = GPIO_MIC_PDM_DATA_PIN,
              .invert_flags = {0},
          },
  };
  cfg->slot_cfg.hp_en = true;
  cfg->slot_cfg.hp_cut_off_freq_hz = 50.0f;
  cfg->slot_cfg.amplify_num = 3;
}

esp_err_t audio_i2s_mic_record(int16_t *out,
                               size_t max_samples,
                               uint32_t sample_rate,
                               size_t *out_captured,
                               audio_mic_level_cb_t cb,
                               void *ctx) {
  if (out == NULL || max_samples == 0)
    return ESP_ERR_INVALID_ARG;
  if (out_captured)
    *out_captured = 0;

  i2s_chan_handle_t rx = NULL;
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &rx);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mic: new_channel %s", esp_err_to_name(err));
    return err;
  }
  i2s_pdm_rx_config_t pdm_cfg;
  mic_pdm_config(&pdm_cfg, sample_rate);
  err = i2s_channel_init_pdm_rx_mode(rx, &pdm_cfg);
  if (err == ESP_OK)
    err = i2s_channel_enable(rx);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mic: init/enable %s", esp_err_to_name(err));
    i2s_del_channel(rx);
    return err;
  }

  vTaskDelay(pdMS_TO_TICKS(120)); // let the HP filter settle

  size_t got = 0;
  size_t first = (max_samples > CHUNK_SAMPLES) ? CHUNK_SAMPLES : max_samples;
  i2s_channel_read(rx, out, first * sizeof(int16_t), &got, pdMS_TO_TICKS(300));

  size_t captured = 0;
  while (captured < max_samples) {
    size_t want =
        (max_samples - captured > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (max_samples - captured);
    err = i2s_channel_read(rx, &out[captured], want * sizeof(int16_t), &got, pdMS_TO_TICKS(500));
    if (err != ESP_OK || got == 0)
      break;
    size_t ns = got / sizeof(int16_t);
    if (cb != NULL) {
      int32_t pk = 0;
      uint64_t sumsq = 0;
      for (size_t i = 0; i < ns; i++) {
        int32_t a = out[captured + i];
        if (a < 0)
          a = -a;
        if (a > pk)
          pk = a;
        sumsq += (uint64_t)a * (uint64_t)a;
      }
      int rms = (ns > 0) ? (int)sqrt((double)(sumsq / ns)) : 0;
      cb((int)pk, rms, ctx);
    }
    captured += ns;
  }

  i2s_channel_disable(rx);
  i2s_del_channel(rx);
  if (out_captured)
    *out_captured = captured;
  ESP_LOGI(TAG,
           "mic: captured %u samples @ %lu Hz (err=%s)",
           (unsigned)captured,
           (unsigned long)sample_rate,
           esp_err_to_name(err));
  return err;
}

esp_err_t audio_i2s_mic_stream_start(uint32_t sample_rate) {
  if (s_rx_stream)
    return ESP_OK;

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_stream);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mic stream: new_channel %s", esp_err_to_name(err));
    s_rx_stream = NULL;
    return err;
  }
  i2s_pdm_rx_config_t pdm_cfg;
  mic_pdm_config(&pdm_cfg, sample_rate);
  err = i2s_channel_init_pdm_rx_mode(s_rx_stream, &pdm_cfg);
  if (err == ESP_OK)
    err = i2s_channel_enable(s_rx_stream);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mic stream: init/enable %s", esp_err_to_name(err));
    i2s_del_channel(s_rx_stream);
    s_rx_stream = NULL;
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(120));
  ESP_LOGI(TAG, "mic stream started @ %lu Hz", (unsigned long)sample_rate);
  return ESP_OK;
}

int audio_i2s_mic_stream_read(int16_t *buf, int max_samples) {
  if (s_rx_stream == NULL || buf == NULL || max_samples <= 0)
    return 0;
  size_t got = 0;
  if (i2s_channel_read(s_rx_stream, buf, max_samples * sizeof(int16_t), &got, pdMS_TO_TICKS(300)) !=
      ESP_OK)
    return 0;
  return (int)(got / sizeof(int16_t));
}

void audio_i2s_mic_stream_stop(void) {
  if (s_rx_stream == NULL)
    return;
  i2s_channel_disable(s_rx_stream);
  i2s_del_channel(s_rx_stream);
  s_rx_stream = NULL;
  ESP_LOGI(TAG, "mic stream stopped");
}
