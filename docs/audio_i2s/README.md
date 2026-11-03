<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# Audio I2S Driver (P4)

## V2.03 control sequence

The amplifier CTRL is C5 GPIO0; P4 GPIO49 is exclusively NFC CS. The kernel
injects `board_service_controls()` through `audio_i2s_bind_controls()` before
initialization. Only the enable level crosses SPI; PCM, volume, I2S and PDM
capture stay local. Initialization allocates TX resources without enabling the
amp or waiting for the C5, so an unavailable C5 does not block microphone use.

Blocking playback APIs run in audio/media workers, not LVGL callbacks. Before
playback, the driver confirms amp off, sets/enables the I2S clock, flushes old
DMA samples with silence, and confirms amp on in the same negotiated session.
Each PCM write checks that session and the known enable state. After a C5 reboot
or stale snapshot, writes stop with an error; old playback is not resumed.
Stopping drains silence and requests confirmed mute. A later explicit playback
request can reconcile the new session. The idle audio worker also retries mute
after an ambiguous/late enable result. Hardware click/pop performance is still
a bench acceptance item; host tests validate ordering and error paths only.

The confirmation helper has a 1200 ms maximum wait, is task-only, and never
calls SPI itself; the board service owns transport. UI feedback remains queued.
Uninitialized/busy one-shot playback now returns INVALID_STATE rather than
reporting a sound as successful. Microphone functions have no amp dependency.


This component drives the on-board audio path of the HighBoy V2 (ESP32-P4 firmware only): a class-D speaker amplifier for playback (TX) and a PDM microphone for capture (RX), both on a single I2S port. It owns one persistent TX channel used for UI chimes, tones, melodies, raw PCM and continuous streaming, and opens independent PDM-RX channels for mic recording and live streaming.

## Overview

- **Location:** `firmware_p4/components/Drivers/audio_i2s/`
- **Header:** `include/audio_i2s.h`
- **Firmware:** `firmware_p4` only (no C5 counterpart)
- **Dependencies:** `driver/i2s_std`, `driver/i2s_pdm`, `driver/gpio`, `freertos`, `pin_def`, `sys_prio`
- **I2S port:** `I2S_NUM_0` (master role). The single TX channel and the PDM-RX channels share this one port.

> **Part naming note:** the public header docstrings refer to a **MAX98357A** amplifier, while the driver source (`audio_i2s.c`) and the pin map (`pin_def.h`) name an **NS4168** amp and an **MSM261** PDM mic. The V2.03 schematic identifies the amp as NS4168 and the mic as LMD3526B21-0FA01; the amplifier is enabled by taking its shutdown pin high, and its hardware gain is fixed, so volume is applied digitally.

## Signal Path

### TX (speaker / amplifier)

- **Mode:** I2S standard (Philips) slot format, **16-bit**, **mono**, master.
- **Default sample rate:** `44100` Hz (`SAMPLE_RATE_HZ`). PCM and stream calls may pass any rate; the driver reconfigures the TX clock only when the requested rate actually changes.
- **One persistent channel:** `I2S_NUM_0` has exactly one TX channel. It is created once at init and never deleted; playback only enables/disables it and reconfigures its clock. Creating/deleting a channel per sound churned internal DMA RAM and stuttered the UI, so that is avoided.
- **DMA depth:** `AUDIO_DMA_DESC_NUM` = 8 descriptors x `AUDIO_DMA_FRAME_NUM` = 256 frames (~46 ms, ~8 KB internal RAM), deeper than the IDF default for underrun slack.
- **Underrun behaviour:** `auto_clear = true`, so a starved channel emits zeros instead of looping the stale buffer.
- **Anti-click:** each note gets a raised-cosine attack (`ENV_ATTACK_MS` = 4 ms) / release (`ENV_RELEASE_MS` = 8 ms) envelope, and playback brackets sounds with short silence; melodies use a continuous phase accumulator for gapless rendering.
- **Amp enable:** confirmed remotely on C5 GPIO0 through the injected board interface.

### RX (PDM microphone)

- **Mode:** PDM RX, **16-bit**, **mono**, master.
- **Filtering:** high-pass filter enabled with a 50 Hz cut-off (`hp_cut_off_freq_hz`), digital gain `amplify_num = 3`. A ~120 ms settle delay is applied after enable to let the HP filter converge.
- **Channels:** the mic uses its own RX channel on `I2S_NUM_0`, independent of the TX side. `audio_i2s_mic_record()` opens/closes a channel per call; the streaming API keeps a persistent RX channel (`s_rx_stream`).

## Concurrency

- Playback runs on a pinned task `"audio_i2s"`: stack `AUDIO_TASK_STACK_SIZE` = 4096, priority `SYS_PRIO_SERVICE_LO`, core `SYS_CORE_RADIO`. It is deliberately **not** on the UI core, because the LVGL renderer at `SYS_PRIO_RENDER` would starve the I2S DMA during a full-frame redraw.
- A single mutex (`s_tx_mux`) guards the shared TX channel. Callers from the LVGL thread use finite timeouts and drop the sound rather than stall the UI:
  - `TX_LOCK_WAIT_MS` = 250 ms for blocking playback / stream claims.
  - `FX_TX_WAIT_MS` = 0 for queued UI effects (never wait behind a melody).
- Queued UI effects (`audio_play_chime`, `audio_click`) post `fx_t` entries to a depth-4 queue (`QUEUE_DEPTH`); overflow is dropped so button mashing never piles up.
- While a TX stream is active (`s_streaming`), one-shot playback calls become no-ops so they do not fight the stream.

## Pins (from `pin_def.h`)

| Signal | Define | GPIO |
| :--- | :--- | :--- |
| Amp enable / shutdown | C5 `GPIO_AUDIO_EN_PIN` | C5 0 |
| I2S bit clock (BCLK) | `GPIO_AUDIO_BCLK_PIN` | 28 |
| I2S word select (LRCLK / WS) | `GPIO_AUDIO_LRCLK_PIN` | 29 |
| I2S data out (DOUT) | `GPIO_AUDIO_DIN_PIN` | 27 |
| Mic PDM clock | `GPIO_MIC_PDM_CLK_PIN` | 54 |
| Mic PDM data | `GPIO_MIC_PDM_DATA_PIN` | 53 |

MCLK and the TX-side DIN are `I2S_GPIO_UNUSED`.

## API Reference

### Initialization

#### `audio_i2s_init`
```c
esp_err_t audio_i2s_init(void);
```
Brings up `I2S_NUM_0` on the amp pins, allocates the DMA chunk buffer, mutex and effect queue, and starts the playback task. Idempotent; safe to call multiple times.

#### `audio_i2s_set_volume`
```c
void audio_i2s_set_volume(uint8_t pct);
```
Sets global output volume (0..100), clamped. Applied as a perceptual (square-law) digital scale on top of each sound's own amplitude, because the amplifier's hardware gain is fixed.

### Tones and Melodies

#### `audio_i2s_play_tone`
```c
esp_err_t audio_i2s_play_tone(float freq_hz, int dur_ms, float amp);
```
Plays a single blocking sine tone at 44.1 kHz mono. `amp` is in [0..1]. Returns `ESP_ERR_TIMEOUT` if the channel is busy (sound dropped), an explicit error when unavailable/busy, and `ESP_OK` on completed playback.

#### `audio_i2s_play_song` / `audio_i2s_play_song_cb`
```c
esp_err_t audio_i2s_play_song(const audio_note_t *notes, int count, float amp);
esp_err_t audio_i2s_play_song_cb(const audio_note_t *notes, int count, float amp,
                                 audio_song_progress_cb_t cb, void *ctx);
```
Plays a melody blocking and gapless: opens the channel once, renders all notes back-to-back with per-note anti-click envelopes and a continuous phase accumulator, then ends. The `_cb` variant invokes `cb` before each note (in the worker thread) with the note index, total count and frequency; returning `false` cancels playback cooperatively after the current note. Pass `cb = NULL` for plain playback.

`audio_note_t` is `{ uint16_t freq_hz; uint16_t dur_ms; }`; `freq_hz == 0` is a rest (silence).

`audio_song_progress_cb_t`:
```c
typedef bool (*audio_song_progress_cb_t)(int note_index, int note_count,
                                         uint16_t freq_hz, void *ctx);
```
Must be trivial (touch only volatile scalars); never call `lv_*` from it.

### Raw PCM Playback

#### `audio_i2s_play_pcm`
```c
esp_err_t audio_i2s_play_pcm(const int16_t *pcm, size_t n_samples, uint32_t sample_rate);
```
Plays a raw 16-bit mono PCM buffer (blocking) at `sample_rate`. Reconfigures the TX clock to the given rate and appends a short tail-silence.

### Continuous TX Streaming

#### `audio_i2s_stream_start` / `audio_i2s_stream_write` / `audio_i2s_stream_stop`
```c
esp_err_t audio_i2s_stream_start(uint32_t sample_rate);
int       audio_i2s_stream_write(const int16_t *pcm, int n_samples);
void      audio_i2s_stream_stop(void);
```
Begins a continuous 16-bit mono TX stream (e.g. WAV playback), reusing the shared TX channel and suppressing UI chimes/clicks for its duration. `stream_write` applies global volume, blocks and paces to real time via the I2S DMA, and returns samples written (or -1 if the stream is not active). `stream_stop` takes the TX mutex before draining and publishing idle; the next
caller selects its clock. A failed drain attempts a checked channel disable;
if disable also fails, the stream remains claimed for a later stop attempt.
Starting another active stream returns `ESP_ERR_INVALID_STATE`. This inherited
stream API is global and does not provide per-caller session ownership.

### PDM Microphone Capture

#### `audio_i2s_mic_record`
```c
esp_err_t audio_i2s_mic_record(int16_t *out, size_t max_samples, uint32_t sample_rate,
                               size_t *out_captured, audio_mic_level_cb_t cb, void *ctx);
```
Captures up to `max_samples` of 16-bit mono PCM from the PDM mic (blocking): opens a PDM-RX channel, lets the HP filter settle (~120 ms), fills `out`, then closes. `*out_captured` receives the real sample count. If `cb` is non-NULL it is invoked after each chunk with that chunk's peak and RMS level for a live VU meter.

`audio_mic_level_cb_t`:
```c
typedef void (*audio_mic_level_cb_t)(int peak, int rms, void *ctx);
```
`peak` is max |sample| (0..32767) of the last chunk. Runs in the recording task's context; keep it trivial.

#### `audio_i2s_mic_stream_start` / `audio_i2s_mic_stream_read` / `audio_i2s_mic_stream_stop`
```c
esp_err_t audio_i2s_mic_stream_start(uint32_t sample_rate);
int       audio_i2s_mic_stream_read(int16_t *buf, int max_samples);
void      audio_i2s_mic_stream_stop(void);
```
Opens a persistent PDM-RX stream at `sample_rate` (idempotent). `mic_stream_read` reads up to `max_samples` 16-bit mono samples, returning the count read (0 on error/timeout/not-started). `mic_stream_stop` stops and frees the RX channel; safe when not started.

### UI Sound Effects

#### `audio_play_chime`
```c
void audio_play_chime(void);
```
Queues a 3-note boot chime (C5 -> E5 -> G5, ~130-180 ms each). Non-blocking; dropped if the queue is full.

#### `audio_click`
```c
void audio_click(void);
```
Queues a short click (~30 ms, ~2 kHz). Non-blocking; dropped if the queue already holds pending entries so rapid button presses never pile up.


## Bounded boot sound

Normal boot launches `boot_sound_play()` only when UI sound effects are enabled.
Recovery never launches it. The optional installed file is
`/assets/storage/ghost_cyberpunk_boot.mp3`; an app-only update does not install it.
A missing file is logged and skipped. Launch success means a worker started,
not that playback completed.

The worker uses an internal 8 KiB stack for VFS/flash operations; input, decoded
frames and final PCM use checked PSRAM allocations. It accepts at most 256 KiB,
8–48 kHz mono/stereo, a constant sample rate and ten seconds of output. A
five-second decode budget, input-progress checks and bounded output reject
malformed or excessive work. The complete file is read before decoding, so an
underflow cannot repeatedly retry the same unrefilled input. It releases all
owned resources on every exit. Low internal heap (64 KiB total / 10 KiB largest
block), allocation failure or decoder/playback error skips this optional sound.

Decoded mono PCM uses the existing blocking, mutex-held playback call; it never
steals an active stream. PCM now applies the global volume. One-shot paths
recheck the streaming flag after taking the mutex. Shared streaming/volume flags
are atomic. DMA drain advances by actual written bytes, rejects zero/odd writes
uses the IDF API's millisecond timeout units and caps drain attempts at 16. Short writes and drain failures
propagate through PCM playback. The original always-disabled boot-sound guard is
replaced by these bounded software paths. Physical audio/cache behavior remains
unverified; no particular historical device crash is claimed reproduced.

`python3 tools/boot_report/test_boot_report.py` exercises the real worker and I2S
control functions with ASan/UBSan and explicit decoder/scheduler/I2S fakes. It
covers malformed/no-progress decoder results, format changes, missing/oversized
files, heap/task failures, claim races, short writes and failed cleanup. These
fixtures do not execute the Helix decoder or actual I2S DMA.
