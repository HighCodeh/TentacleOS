// HighBoy DOOM sound backend (replaces the SDL_mixer module). Provides the
// DG_sound_module (SFX) + DG_music_module (no-op) that i_sound.c references when
// FEATURE_SOUND is defined. SFX are DMX lumps (8-bit unsigned PCM); a mixer task
// sums the active channels and streams mono 16-bit PCM to the NS4168 via I2S.
// Music (MIDI/MUS) is not synthesised — the music module is a silent stub.

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "audio_i2s.h"

#define ARRLEN(a)  ((int)(sizeof(a) / sizeof((a)[0])))
#define NUM_CH     16       // matches DOOM's max sfx channels
#define OUT_RATE   11025    // DOOM sfx native rate; per-channel resample handles others

typedef struct {
  const uint8_t *pcm;      // 8-bit unsigned PCM (into the cached WAD lump)
  uint32_t len;            // sample count
  volatile uint32_t pos;   // 16.16 fixed-point read position
  uint32_t step;           // 16.16 increment = src_rate / OUT_RATE
  volatile int vol;        // 0..127
  volatile bool active;
} sfxchan_t;

static sfxchan_t s_ch[NUM_CH];
static bool s_use_prefix;
static bool s_started;

// Config vars that i_sound.c's I_BindSoundVariables() references (were defined
// in the excluded i_sdlsound.c). We don't use libsamplerate, but must provide them.
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

static snddevice_t sfx_devices[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

static void mixer_task(void *arg) {
  (void)arg;
  if (audio_i2s_stream_start(OUT_RATE) != ESP_OK) {
    ESP_LOGE("DOOM_SND", "stream start failed — no sound");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  const int N = 512;
  int16_t *out = heap_caps_malloc(N * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!out) { for (;;) vTaskDelay(pdMS_TO_TICKS(1000)); }
  for (;;) {
    for (int i = 0; i < N; i++) {
      int acc = 0;
      for (int c = 0; c < NUM_CH; c++) {
        sfxchan_t *ch = &s_ch[c];
        if (!ch->active) continue;
        uint32_t idx = ch->pos >> 16;
        if (idx >= ch->len) { ch->active = false; continue; }
        int s = (int)ch->pcm[idx] - 128;   // 8-bit unsigned -> signed
        acc += s * ch->vol;                // vol 0..127
        ch->pos += ch->step;
      }
      if (acc > 32767) acc = 32767;
      else if (acc < -32768) acc = -32768;
      out[i] = (int16_t)acc;
    }
    audio_i2s_stream_write(out, N);
  }
}

static boolean I_HB_InitSound(boolean use_sfx_prefix) {
  s_use_prefix = use_sfx_prefix;
  memset(s_ch, 0, sizeof(s_ch));
  if (!s_started) {
    xTaskCreatePinnedToCoreWithCaps(mixer_task, "doom_snd", 4096, NULL, 6, NULL, 0,
                                    MALLOC_CAP_SPIRAM);
    s_started = true;
  }
  return true;
}
static void I_HB_ShutdownSound(void) {}

static int I_HB_GetSfxLumpNum(sfxinfo_t *sfx) {
  char nm[16];
  if (s_use_prefix) snprintf(nm, sizeof(nm), "ds%s", sfx->name);
  else snprintf(nm, sizeof(nm), "%s", sfx->name);
  return W_CheckNumForName(nm);
}

static int I_HB_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep) {
  (void)sep;
  if (channel < 0 || channel >= NUM_CH) return channel;
  int lump = sfx->lumpnum;
  if (lump < 0) lump = I_HB_GetSfxLumpNum(sfx);
  if (lump < 0) return channel;
  const uint8_t *data = W_CacheLumpNum(lump, PU_STATIC);
  int lumplen = W_LumpLength(lump);
  if (!data || lumplen < 8) return channel;
  uint16_t rate = (uint16_t)(data[2] | (data[3] << 8));
  uint32_t plen = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                  ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
  if (rate == 0) rate = OUT_RATE;
  if (plen + 8 > (uint32_t)lumplen) plen = (uint32_t)lumplen - 8;

  s_ch[channel].active = false;               // retire any old sound first
  s_ch[channel].pcm = data + 8;               // skip 8-byte DMX header
  s_ch[channel].len = plen;
  s_ch[channel].pos = 0;
  s_ch[channel].step = (uint32_t)(((uint64_t)rate << 16) / OUT_RATE);
  s_ch[channel].vol = vol;
  s_ch[channel].active = true;
  return channel;
}
static void I_HB_StopSound(int ch) { if (ch >= 0 && ch < NUM_CH) s_ch[ch].active = false; }
static boolean I_HB_SoundIsPlaying(int ch) { return (ch >= 0 && ch < NUM_CH) && s_ch[ch].active; }
static void I_HB_UpdateSoundParams(int ch, int vol, int sep) {
  (void)sep;
  if (ch >= 0 && ch < NUM_CH) s_ch[ch].vol = vol;
}
static void I_HB_Update(void) {}
static void I_HB_CacheSounds(sfxinfo_t *sounds, int num) { (void)sounds; (void)num; }

sound_module_t DG_sound_module = {
    sfx_devices, ARRLEN(sfx_devices),
    I_HB_InitSound, I_HB_ShutdownSound, I_HB_GetSfxLumpNum, I_HB_Update,
    I_HB_UpdateSoundParams, I_HB_StartSound, I_HB_StopSound, I_HB_SoundIsPlaying,
    I_HB_CacheSounds,
};

// --- music: silent stub -----------------------------------------------------
static snddevice_t mus_devices[] = {
    SNDDEVICE_GENMIDI, SNDDEVICE_ADLIB, SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
};
static boolean M_Init(void) { return true; }
static void M_Shutdown(void) {}
static void M_SetVol(int v) { (void)v; }
static void M_Pause(void) {}
static void M_Resume(void) {}
static void *M_Register(void *d, int l) { (void)d; (void)l; return NULL; }
static void M_Unregister(void *h) { (void)h; }
static void M_Play(void *h, boolean loop) { (void)h; (void)loop; }
static void M_Stop(void) {}
static boolean M_IsPlaying(void) { return false; }
static void M_Poll(void) {}

music_module_t DG_music_module = {
    mus_devices, ARRLEN(mus_devices),
    M_Init, M_Shutdown, M_SetVol, M_Pause, M_Resume,
    M_Register, M_Unregister, M_Play, M_Stop, M_IsPlaying, M_Poll,
};
