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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef struct { unsigned rate; } i2s_std_clk_config_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_INVALID_RESPONSE 5
#define I2S_STD_CLK_DEFAULT_CONFIG(value) ((i2s_std_clk_config_t){value})
#define pdMS_TO_TICKS(value) ((value)/10)
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
static void fixture_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
/* CONSTANTS */
static const char *TAG="TEST";
static esp_err_t s_play_error;
static bool s_amp;
static esp_err_t s_amp_fail;
static esp_err_t amp_set(bool v) { if (s_amp_fail) return s_amp_fail; s_amp=v; return ESP_OK; }
static bool amp_confirmed(void) { return s_amp; }
static bool s_ready=true,s_tx_enabled=true,s_locked,s_fail_lock,s_claim_on_lock;
static atomic_bool s_streaming;
static _Atomic(float) s_play_vol=1.0f;
static uint32_t s_tx_rate=44100;
static void *s_tx=(void *)1;
static int16_t s_storage[1024];
static int16_t *s_chunk_buf=s_storage;
static unsigned s_write_mode,s_calls,s_disables,s_enable_fail,s_disable_fail;
static size_t s_bytes;
static bool tx_lock(TickType_t wait) {
  (void)wait;if(s_fail_lock)return false;assert(!s_locked);s_locked=true;
  if(s_claim_on_lock)s_streaming=true;
  return true;
}
static void tx_unlock(void) { assert(s_locked);s_locked=false; }
static esp_err_t i2s_channel_write(void *channel,const void *buffer,size_t bytes,size_t *out,unsigned timeout) {
  (void)buffer;assert(s_locked && channel==s_tx && (timeout==300 || timeout==500));++s_calls;
  *out=s_write_mode==1?0:s_write_mode==2?1:s_write_mode==3?(bytes<256?bytes:256):s_write_mode==5?2:bytes;
  s_bytes+=*out;return s_write_mode==4?ESP_FAIL:ESP_OK;
}
static esp_err_t i2s_channel_disable(void *channel) {assert(channel==s_tx);++s_disables;return s_disable_fail?ESP_FAIL:ESP_OK;}
static esp_err_t i2s_channel_enable(void *channel) {assert(channel==s_tx);return s_enable_fail?ESP_FAIL:ESP_OK;}
static esp_err_t i2s_channel_reconfig_std_clock(void *channel,const i2s_std_clk_config_t *clock) {(void)clock;assert(channel==s_tx);return ESP_OK;}
static const char *esp_err_to_name(esp_err_t result) {(void)result;return "fixture";}
/* MODULE */
int main(void) {
  s_locked=true;s_write_mode=3;assert(tx_drain()==ESP_OK);
  assert(s_calls==16 && s_bytes==4096);
  for(unsigned mode=1;mode<=4;++mode) {
    s_write_mode=mode;if(mode==3)continue;
    unsigned before=s_calls;assert(tx_drain()!=ESP_OK && s_calls==before+1);
  }
  s_write_mode=5;assert(tx_drain()==ESP_ERR_TIMEOUT);
  s_write_mode=0;s_locked=false;
  assert(audio_i2s_stream_start(44100)==ESP_OK && s_streaming);
  assert(audio_i2s_stream_start(48000)==ESP_ERR_INVALID_STATE && s_tx_rate==44100);
  int16_t pcm[]={1000,2000};
  s_play_vol=0.5f;
  assert(audio_i2s_stream_write(pcm,2)==2 && s_storage[0]==500 && s_storage[1]==1000);
  s_fail_lock=true;audio_i2s_stream_stop();assert(s_streaming);s_fail_lock=false;
  s_write_mode=4;s_disable_fail=1;audio_i2s_stream_stop();assert(s_streaming);
  s_disable_fail=0;audio_i2s_stream_stop();assert(!s_streaming && !s_tx_enabled && s_disables>=2);
  s_write_mode=0;
  s_claim_on_lock=true;assert(audio_i2s_play_pcm(pcm,2,44100)==ESP_ERR_INVALID_STATE && !s_locked);
  s_claim_on_lock=false;s_streaming=false;
  assert(audio_i2s_play_pcm(pcm,2,44100)==ESP_OK && !s_locked);
  s_write_mode=1;assert(audio_i2s_play_pcm(pcm,2,44100)!=ESP_OK && !s_locked);
  s_streaming=false;assert(audio_i2s_stream_write(pcm,2)==-1);
  s_streaming=false;s_write_mode=0;s_amp_fail=ESP_ERR_INVALID_STATE;
  unsigned before_calls=s_calls;
  assert(audio_i2s_stream_start(44100)==ESP_ERR_INVALID_STATE && s_calls==before_calls);
  s_amp_fail=ESP_OK;
  assert(audio_i2s_stream_start(44100)==ESP_OK && s_amp);
  s_amp=false;
  assert(audio_i2s_stream_write(pcm,2)==-1);
  audio_i2s_stream_stop();
  assert(!s_streaming && !s_amp);
  assert(audio_i2s_stream_start(44100)==ESP_OK && s_amp);
  audio_i2s_stream_stop();
  assert(!s_amp);
  puts("PASS: real I2S drain progress, timeouts in milliseconds, short writes, PCM volume, claim races and failed-stop retention");
}
