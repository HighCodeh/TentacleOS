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

#include "audio_i2s.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pin_def.h"

static const char *TAG = "AUDIO_I2S";

#define SAMPLE_RATE_HZ   44100
#define CHUNK_SAMPLES    1024
#define MAX_TONES_PER_FX 4
#define QUEUE_DEPTH      4

#define AUDIO_TASK_STACK_SIZE 3072
#define AUDIO_TASK_PRIORITY   4
#define AUDIO_TASK_CORE       1

typedef struct {
  float freq_hz;
  uint16_t dur_ms;
  float amp;
} tone_t;

typedef struct {
  tone_t tones[MAX_TONES_PER_FX];
  uint8_t count;
} fx_t;

static i2s_chan_handle_t s_tx = NULL;
static QueueHandle_t s_fx_q = NULL;
static int16_t *s_chunk_buf = NULL;
static bool s_ready = false;
static i2s_chan_handle_t s_rx_stream = NULL;
static i2s_chan_handle_t s_stream_tx = NULL;
static volatile bool s_streaming = false;
static int16_t *s_stream_buf = NULL;

static float s_play_vol = 1.0f;

void audio_i2s_set_volume(uint8_t pct) {
  if (pct > 100)
    pct = 100;
  float n = pct / 100.0f;
  s_play_vol = n * n;
}

static void play_tone(float freq_hz, int dur_ms, float amp) {
  const int total = (SAMPLE_RATE_HZ * dur_ms) / 1000;
  for (int phase_i = 0; phase_i < total;) {
    int n = (total - phase_i > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - phase_i);
    for (int i = 0; i < n; i++) {
      double t = (double)(phase_i + i) / SAMPLE_RATE_HZ;
      s_chunk_buf[i] = (int16_t)(sinf(2.0f * 3.14159265f * freq_hz * t) * amp * 32760.0f);
    }
    size_t written = 0;
    i2s_channel_write(s_tx, s_chunk_buf, n * sizeof(int16_t), &written, pdMS_TO_TICKS(500));
    phase_i += n;
  }
}

static void play_silence(int dur_ms) {
  const int total = (SAMPLE_RATE_HZ * dur_ms) / 1000;
  memset(s_chunk_buf, 0, CHUNK_SAMPLES * sizeof(int16_t));
  for (int phase_i = 0; phase_i < total;) {
    int n = (total - phase_i > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - phase_i);
    size_t written = 0;
    i2s_channel_write(s_tx, s_chunk_buf, n * sizeof(int16_t), &written, pdMS_TO_TICKS(500));
    phase_i += n;
  }
}

static void audio_task(void *arg) {
  (void)arg;
  play_silence(50);

  fx_t fx;
  while (true) {
    if (xQueueReceive(s_fx_q, &fx, portMAX_DELAY) != pdTRUE)
      continue;
    if (s_streaming)
      continue;
    for (int i = 0; i < fx.count; i++) {
      play_tone(fx.tones[i].freq_hz, fx.tones[i].dur_ms, fx.tones[i].amp);
    }
    play_silence(5);
  }
}

esp_err_t audio_i2s_init(void) {
  if (s_ready)
    return ESP_OK;

  // Take the NS4168 amplifier out of shutdown (CTRL/EN high) before streaming.
  gpio_config_t en_cfg = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ULL << GPIO_AUDIO_EN_PIN,
      .intr_type = GPIO_INTR_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
  };
  gpio_config(&en_cfg);
  gpio_set_level(GPIO_AUDIO_EN_PIN, 1);

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
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
  err = i2s_channel_enable(s_tx);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "channel_enable: %s", esp_err_to_name(err));
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return err;
  }

  s_chunk_buf = heap_caps_malloc(CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_DMA);
  if (s_chunk_buf == NULL) {
    ESP_LOGE(TAG, "chunk buf alloc failed");
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_fx_q = xQueueCreate(QUEUE_DEPTH, sizeof(fx_t));
  if (s_fx_q == NULL) {
    free(s_chunk_buf);
    s_chunk_buf = NULL;
    i2s_channel_disable(s_tx);
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
    free(s_chunk_buf);
    s_chunk_buf = NULL;
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_ready = true;
  ESP_LOGI(TAG,
           "audio I2S ready (BCLK=%d WS=%d DOUT=%d @ %d Hz)",
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

static esp_err_t open_tx(i2s_chan_handle_t *tx, int rate) {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&chan_cfg, tx, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "open_tx: new_channel %s", esp_err_to_name(err));
    return err;
  }
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
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
  err = i2s_channel_init_std_mode(*tx, &std_cfg);
  if (err == ESP_OK)
    err = i2s_channel_enable(*tx);
  if (err != ESP_OK) {
    i2s_del_channel(*tx);
    *tx = NULL;
  }
  return err;
}

static void write_silence(i2s_chan_handle_t tx, int16_t *buf, int rate, int dur_ms) {
  const int total = (rate * dur_ms) / 1000;
  memset(buf, 0, CHUNK_SAMPLES * sizeof(int16_t));
  size_t w = 0;
  for (int p = 0; p < total;) {
    int n = (total - p > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - p);
    i2s_channel_write(tx, buf, n * sizeof(int16_t), &w, pdMS_TO_TICKS(500));
    p += n;
  }
}

#define ENV_ATTACK_MS  4
#define ENV_RELEASE_MS 8

static void render_note(i2s_chan_handle_t tx, int16_t *buf, float freq_hz, int dur_ms, float amp) {
  const int total = (SAMPLE_RATE_HZ * dur_ms) / 1000;
  if (total <= 0)
    return;
  const int atk = (SAMPLE_RATE_HZ * ENV_ATTACK_MS) / 1000;
  const int rel = (SAMPLE_RATE_HZ * ENV_RELEASE_MS) / 1000;
  const float vol = amp * s_play_vol;
  const float dphase = 2.0f * 3.14159265f * freq_hz / (float)SAMPLE_RATE_HZ;
  float phase = 0.0f;
  size_t w = 0;
  for (int p = 0; p < total;) {
    int n = (total - p > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - p);
    for (int i = 0; i < n; i++) {
      if (freq_hz <= 0.0f) {
        buf[i] = 0;
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
      buf[i] = (int16_t)v;
    }
    i2s_channel_write(tx, buf, n * sizeof(int16_t), &w, pdMS_TO_TICKS(500));
    p += n;
  }
}

esp_err_t audio_i2s_play_tone(float freq_hz, int dur_ms, float amp) {
  if (s_streaming)
    return ESP_OK;
  i2s_chan_handle_t tx = NULL;
  esp_err_t err = open_tx(&tx, SAMPLE_RATE_HZ);
  if (err != ESP_OK)
    return err;
  int16_t *buf = heap_caps_malloc(CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_DMA);
  if (buf == NULL) {
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    return ESP_ERR_NO_MEM;
  }
  write_silence(tx, buf, SAMPLE_RATE_HZ, 8);
  render_note(tx, buf, freq_hz, dur_ms, amp);
  write_silence(tx, buf, SAMPLE_RATE_HZ, 8);
  free(buf);
  i2s_channel_disable(tx);
  i2s_del_channel(tx);
  return ESP_OK;
}

esp_err_t audio_i2s_play_song_cb(
    const audio_note_t *notes, int count, float amp, audio_song_progress_cb_t cb, void *ctx) {
  if (notes == NULL || count <= 0)
    return ESP_ERR_INVALID_ARG;
  if (s_streaming)
    return ESP_OK;
  i2s_chan_handle_t tx = NULL;
  esp_err_t err = open_tx(&tx, SAMPLE_RATE_HZ);
  if (err != ESP_OK)
    return err;
  int16_t *buf = heap_caps_malloc(CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_DMA);
  if (buf == NULL) {
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    return ESP_ERR_NO_MEM;
  }
  write_silence(tx, buf, SAMPLE_RATE_HZ, 8);
  for (int i = 0; i < count; i++) {
    if (cb != NULL && !cb(i, count, notes[i].freq_hz, ctx))
      break;
    render_note(tx, buf, (float)notes[i].freq_hz, notes[i].dur_ms, amp);
  }
  write_silence(tx, buf, SAMPLE_RATE_HZ, 8);
  free(buf);
  i2s_channel_disable(tx);
  i2s_del_channel(tx);
  return ESP_OK;
}

esp_err_t audio_i2s_play_song(const audio_note_t *notes, int count, float amp) {
  return audio_i2s_play_song_cb(notes, count, amp, NULL, NULL);
}

esp_err_t audio_i2s_play_pcm(const int16_t *pcm, size_t n_samples, uint32_t sample_rate) {
  if (pcm == NULL || n_samples == 0)
    return ESP_ERR_INVALID_ARG;
  if (s_streaming)
    return ESP_OK;
  i2s_chan_handle_t tx = NULL;
  esp_err_t err = open_tx(&tx, (int)sample_rate);
  if (err != ESP_OK)
    return err;

  size_t off = 0, w = 0;
  while (off < n_samples) {
    size_t chunk = (n_samples - off > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (n_samples - off);
    err = i2s_channel_write(tx, &pcm[off], chunk * sizeof(int16_t), &w, pdMS_TO_TICKS(500));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "play_pcm: write @ %u failed: %s", (unsigned)off, esp_err_to_name(err));
      break;
    }
    off += chunk;
  }
  if (err == ESP_OK) {
    int16_t zeros[256] = {0};
    int tail = (int)(sample_rate * 20 / 1000);
    while (tail > 0) {
      int n = tail > 256 ? 256 : tail;
      i2s_channel_write(tx, zeros, n * sizeof(int16_t), &w, pdMS_TO_TICKS(200));
      tail -= n;
    }
  }

  i2s_channel_disable(tx);
  i2s_del_channel(tx);
  return err;
}

#define STREAM_OPEN_RETRIES  12
#define STREAM_OPEN_RETRY_MS 30

esp_err_t audio_i2s_stream_start(uint32_t sample_rate) {
  if (s_stream_tx != NULL)
    audio_i2s_stream_stop();
  if (s_stream_buf == NULL) {
    s_stream_buf = heap_caps_malloc(CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_DMA);
    if (s_stream_buf == NULL)
      return ESP_ERR_NO_MEM;
  }

  s_streaming = true;

  esp_err_t err = ESP_FAIL;
  for (int attempt = 0; attempt < STREAM_OPEN_RETRIES; attempt++) {
    err = open_tx(&s_stream_tx, (int)sample_rate);
    if (err == ESP_OK)
      break;
    s_stream_tx = NULL;
    vTaskDelay(pdMS_TO_TICKS(STREAM_OPEN_RETRY_MS));
  }
  if (err != ESP_OK) {
    s_streaming = false;
    s_stream_tx = NULL;
    ESP_LOGE(TAG, "stream_start: open_tx %s", esp_err_to_name(err));
  }
  return err;
}

int audio_i2s_stream_write(const int16_t *pcm, int n_samples) {
  if (s_stream_tx == NULL || s_stream_buf == NULL || pcm == NULL || n_samples <= 0)
    return -1;
  int done = 0;
  while (done < n_samples) {
    int n = (n_samples - done > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (n_samples - done);
    for (int i = 0; i < n; i++) {
      int v = (int)(pcm[done + i] * s_play_vol);
      if (v > 32767)
        v = 32767;
      else if (v < -32768)
        v = -32768;
      s_stream_buf[i] = (int16_t)v;
    }
    size_t w = 0;
    if (i2s_channel_write(s_stream_tx, s_stream_buf, n * sizeof(int16_t), &w, pdMS_TO_TICKS(500)) !=
        ESP_OK)
      return done;
    done += n;
  }
  return done;
}

void audio_i2s_stream_stop(void) {
  if (s_stream_tx == NULL) {
    s_streaming = false;
    return;
  }

  if (s_stream_buf != NULL) {
    memset(s_stream_buf, 0, CHUNK_SAMPLES * sizeof(int16_t));
    size_t w = 0;
    for (int k = 0; k < 3; k++)
      i2s_channel_write(
          s_stream_tx, s_stream_buf, CHUNK_SAMPLES * sizeof(int16_t), &w, pdMS_TO_TICKS(100));
  }
  i2s_channel_disable(s_stream_tx);
  i2s_del_channel(s_stream_tx);
  s_stream_tx = NULL;
  s_streaming = false;
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
  i2s_pdm_rx_config_t pdm_cfg = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .clk = GPIO_MIC_PDM_CLK_PIN,
              .din = GPIO_MIC_PDM_DATA_PIN,
              .invert_flags = {0},
          },
  };
  pdm_cfg.slot_cfg.hp_en = true;
  pdm_cfg.slot_cfg.hp_cut_off_freq_hz = 50.0f;
  pdm_cfg.slot_cfg.amplify_num = 3;
  err = i2s_channel_init_pdm_rx_mode(rx, &pdm_cfg);
  if (err == ESP_OK)
    err = i2s_channel_enable(rx);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mic: init/enable %s", esp_err_to_name(err));
    i2s_del_channel(rx);
    return err;
  }

  vTaskDelay(pdMS_TO_TICKS(120));

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
  i2s_pdm_rx_config_t pdm_cfg = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .clk = GPIO_MIC_PDM_CLK_PIN,
              .din = GPIO_MIC_PDM_DATA_PIN,
              .invert_flags = {0},
          },
  };
  pdm_cfg.slot_cfg.hp_en = true;
  pdm_cfg.slot_cfg.hp_cut_off_freq_hz = 50.0f;
  pdm_cfg.slot_cfg.amplify_num = 3;
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
