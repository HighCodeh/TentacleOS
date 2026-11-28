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

# SubGhz Application

This component implements the complete Sub-GHz RF application layer: signal reception (with protocol decoding and frequency hopping), raw/encoded transmission, spectrum analysis, signal analysis, and file serialization. It sits on top of the `cc1101` driver and uses the ESP-IDF RMT peripheral for precise pulse timing.

Carrier validation and High Boy's four-branch antenna matching policy are shared
through the [CC1101 driver](../cc1101/README.md#high-boy-v203-matching-network).
The driver configures presets in IDLE and enters RX only after tuning and
calibration succeed; the application does not override the switch GPIOs.

**Radio Config** selects AM270, AM650, FM238 or FM476, plus the existing High Boy
AM812/FM952 profiles. Selecting a preset refreshes the displayed modulation,
bandwidth, data rate, deviation and LNA/DVGA gain limits. LEFT/RIGHT or OK edits
bandwidth, rate, FSK deviation or either gain limit independently; the preset
name gains `*` when values differ from its defaults. Modulation follows the
preset, and deviation is unavailable for OOK. `Reset preset` restores the base
values without changing frequency. Selecting another preset discards overrides.
No Apply action is required for modem fields.

Radio Config starts with an inline frequency dial: a large carrier value above a
ruler of ticks and a center marker. Its gradient spans the full card height in
both orientations, rather than repeating the shorter menu-row tile. The remaining preset/modem/gain rows retain
their previous layout underneath. UP/DOWN selects the dial or a settings row;
LEFT/RIGHT (including held repeat) immediately steps through 52 ascending fixed
carriers followed by Hop bands, wrapping at either end. OK does not change the
dial. No separate list or confirmation screen opens. The number omits trailing
zeroes but retains exact Hz precision (433.657070 MHz displays as 433.65707).
Changing frequency preserves preset and custom modem/gain settings. The settings
catalog stays inside the driver's 300–348, 387–464 and 779–928 MHz bands. This
list does not expand the existing hopping sequence or change the four-branch RF policy.
The ruler indicates discrete selection steps, not a measured spectrum or a
linear frequency scale. Settings remain in RAM across navigation. The shared setter rejects carriers outside
the supported bands without changing the previous selection; zero remains hopping.

Read copies the configuration into the native receiver, which applies the full
profile and overrides through `cc1101_set_radio_config()` before starting RMT.
The receiver's RAW mode accepts the same configuration. The configuration screen
does not acquire or power the radio. Settings survive navigation in RAM, not a
reboot; the default remains AM270 at 433.92 MHz. Frequency/hopping is selected
separately and preserves overrides. Saved files retain the capture's preset,
frequency and customized radio values; replay uses those instead of current
menu settings. See the [driver contract](../cc1101/README.md#preset-management)
for gain semantics and configuration validation.

## Overview

- **Location:** `components/Applications/SubGhz/`
- **Dependencies:** `cc1101`, `driver/rmt_rx`, `driver/rmt_tx`, `freertos`, `pin_def`
- **RMT Resolution:** 1 MHz (1 us per tick)
- **RX GPIO:** GPIO 8 (GDO0 via `GPIO_SDA_PIN`)
- **TX GPIO:** GPIO 8 / CC1101 GDO0 (`GPIO_CC1101_GDO0_PIN`)

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    SubGhz App                       │
│                                                     │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────┐ │
│  │ Receiver │  │ Transmitter  │  │   Spectrum    │ │
│  │ (RMT RX) │  │  (RMT TX)   │  │   Analyzer    │ │
│  └────┬─────┘  └──────┬───────┘  └───────┬───────┘ │
│       │               │                  │          │
│  ┌────┴─────┐    ┌────┴─────┐     ┌──────┴───────┐ │
│  │ Protocol │    │  Queue   │     │ RSSI Sweep   │ │
│  │ Registry │    │  Worker  │     │   (80 bins)  │ │
│  └────┬─────┘    └──────────┘     └──────────────┘ │
│       │                                             │
│  ┌────┴─────┐  ┌──────────────┐  ┌───────────────┐ │
│  │ Analyzer │  │  Serializer  │  │   Storage     │ │
│  │(Histogram)│  │ (.sub files) │  │  (SD Card)   │ │
│  └──────────┘  └──────────────┘  └───────────────┘ │
└─────────────────────────────────────────────────────┘
                        │
              ┌─────────┴─────────┐
              │   CC1101 Driver   │
              │     (SPI Bus)     │
              └───────────────────┘
```

## Modules

### Receiver (`subghz_receiver`)

Captures RF signals via the CC1101 GDO0 pin routed to the ESP32 RMT RX peripheral. Runs as a FreeRTOS task pinned to `SYS_CORE_RADIO` (core 0).

**Operating Modes:**

| Mode | Behavior |
|------|----------|
| `SUBGHZ_MODE_SCAN` | Collects a bounded burst before decoding. Plausible unknown or conflicting captures remain RAW in memory until explicitly saved; unrecognized noise resumes listening. |
| `SUBGHZ_MODE_RAW` | Streams RAW blocks to a hidden temporary SD file until OK/finish; Save names the file, Back discards it. |

**Frequency Hopping:** When `freq == 0` is passed to `subghz_receiver_start`, the receiver cycles through 12 predefined frequencies (433.92, 868.35, 315, 300, 390, 418, 915 MHz, etc.) with a nominal 100 ms dwell per quiet frequency (about 1.2 seconds per 12-frequency
cycle, plus tuning and task overhead). RSSI above -90 dBm holds the channel for
up to one second to receive a frame. Once a qualifying pulse block arrives, the
existing bounded burst collector keeps that frequency until completion. Strong
unmodulated interference without a frame cannot hold the search indefinitely.
Pending RMT events are consumed before a hop; switching drains old-channel events
and rearms DMA. These timings do not guarantee reception of short bursts between
visits. This follows the RSSI-pause approach in
[Flipper's hopper](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/subghz/helpers/subghz_txrx.c)
without copying its implementation.

**Signal Processing Pipeline:**
1. RMT hardware captures pulse timings (min 1 us, idle timeout 30 ms)
2. Software filter removes pulses < 15 us
3. Pulses converted to signed int32 buffer (positive = HIGH, negative = LOW)
4. **SCAN / Read:** After at least eight filtered pulses, freeze channel hopping and
   append RMT blocks in PSRAM. No decoder or file write runs while collecting.
   Poll RSSI every 50 ms and publish a synchronized 64-sample chronological history
   plus pulse/block counts, frequency and elapsed time for the UI.
5. Finalize after 350 ms without qualifying activity. New pulse blocks reset that
   interval; RSSI more than 8 dB above the pre-capture noise estimate also postpones
   it. This is a silence heuristic, not proof that every transmitter has stopped.
   The waiting noise estimate uses a 1/8 exponential update and resets on hopping.
6. Disable RMT before analysis. Decode each collected block, group matches by
   protocol/key/bit count/serial/button, and choose the uniquely most frequent
   candidate. Ties remain unknown; if no block decodes, try the aggregate buffer
   to recover a frame crossing a block boundary. Single matches are reported as
   such, not as verified repeat consensus. Known decoded signals bypass the noise
   gate, including fast protocols. An unrecognized burst must contain a run of
   16 alternating pulses, each 80..4000 us, with adjacent duration ratios at most
   8:1 before automatic RAW fallback. This is a conservative timing plausibility
   heuristic, not protocol identification or proof of a real transmitter. No pulse
   is edited and no missing bit is reconstructed. Explicit finish bypasses this
   gate; Read RAW keeps its existing capture path for unusual/fast signals.
   Rejected bursts reset only burst counters/timestamps, rearm the same RMT buffer,
   release hopping hold and return to listening. BACK still cancels; rearm failures
   follow normal error cleanup. The UI reports "Noise filtered / listening";
   rejections are DEBUG-only to avoid repeated warning/LED activity. RSSI values
   remain unsmoothed and are not used as a hard minimum signal-strength gate.
   The timing analyzer sees all pulses of accepted captures.

   Validation: `tools/highboy/test_subghz_noise.py` checks timing/polarity bounds
   and accepts optional private `--accept`/`--reject` RAW captures. The supplied
   nearby 1201-pulse capture passes; the distant 55-pulse fallback fails the gate.
   Neither private capture is embedded in the repository. The RX lifecycle test
   covers noise followed by a valid signal, cancellation, failed rearm and unchanged
   long RAW streaming. This does not establish improved RF sensitivity or range.
7. Serialize the decoded result or RAW waveform in PSRAM while the capture's radio
   configuration is still active, publish one result, then release the radio.
   There is no SD access during capture or analysis. The bounded 64 KiB pending
   snapshot retains frequency/preset/custom modem fields even after radio cleanup.
8. **RAW mode:** Use partial RMT DMA reception and copy each callback's completed
   symbols into a bounded 16-block PSRAM ring before the DMA buffer can be reused.
   The worker drains that ring into a hidden `.raw-XXXXXXXX.tmp` file on microSD,
   in `RAW_Data` lines of at most 512 signed pulses. The first accepted pulses freeze
   hopping. Quiet periods do not complete capture. There is no total 8192-pulse,
   128-block or 10-second limit in Read RAW. The microSD must be available.
   OK requests finish, allows up to 100 ms for a quiet-boundary DMA tail, disables
   RMT, drains completed queued blocks and checks file close. BACK cancels/discards.
   If the ring overflows, SD writing fails or close fails, no successful result is
   published and the temporary capture is removed; the UI reports an RX error.

The result's Save action asks for a name. Read writes its PSRAM snapshot; Read RAW
renames its completed temporary file to the requested `.sub` name. Existing names
are never overwritten. A failed Save retains the pending result for retry.
Back, Discard, Capture again and the screen close hook release pending captures.
Send retains a reference to an unsaved RAW file before navigation; the last owner
removes it unless Save has made it permanent. Saved files are not deleted by replay.
An unexpected power loss may leave a hidden temporary file; new sessions never
reuse or overwrite it. Temporary files are excluded from the Saved list.

**Read bounds and RAW streaming memory.**

Decoded Read remains bounded to 8192 pulses, 128 RMT blocks or 10 seconds after
its first qualifying block, with explicit `is_limited` feedback. Its collector,
candidate table and 64 KiB serialized snapshot use checked PSRAM allocations.
Read RAW instead uses a 16 KiB, 64-byte-aligned DMA buffer and a 256 KiB PSRAM ring.
Its write batch and bounded pulse conversion workspace also use PSRAM. Progress
counters saturate at UINT32_MAX without ending capture; elapsed time is tick-based.
`mem_block_symbols` matches the RX buffer capacity (1024 symbols for Read, 4096
for RAW). Internal memory holds the SDK channel, descriptors and synchronization;
there is no bulk allocation fallback to internal RAM. These buffers are freed
only after the RMT channel has stopped and been deleted.

IDF 6.1's P4 RX driver supports external GDMA buffers and cache invalidation before
callbacks. In RAW partial mode, a full DMA block does not require restarting RX.
The 30 ms hardware idle boundary still requires rearming, done in the callback
before SD work. This does not promise zero lost edges across rearming intervals,
nor exact wall-clock preservation of long idle gaps between separate acquisitions.
Manual finish may omit an incomplete hardware tail during an uninterrupted signal;
completed blocks are drained. This is streaming capture, not SDR/IQ acquisition.

`subghz_receiver_get_progress()` returns a snapshot; the UI never reads CC1101
registers. Back cancels collection and does not analyze a partial cancelled burst.
Worker errors, teardown retries and resource ownership follow the same lifecycle
as before. The host lifecycle tests exercise quiet completion, deferred decoding,
majority evidence, frame/time bounds, live RSSI and cancellation; RF timing and
reception quality still require board tests.

**LED feedback:** The RX pipeline drives the status LED as it processes signals: `led_signal_info()` fires when a known protocol is decoded, and `led_signal_warning()` fires when RF is captured but no registered protocol matches.

#### API

```c
esp_err_t subghz_receiver_start(subghz_mode_t mode, cc1101_preset_t preset, uint32_t freq);
esp_err_t subghz_receiver_start_configured(subghz_mode_t mode,
                                         const cc1101_radio_config_t *config, uint32_t freq);
bool subghz_receiver_get_progress(subghz_rx_progress_t *out_progress);
esp_err_t subghz_receiver_save(uint32_t sequence, const char *name);
esp_err_t subghz_receiver_copy_capture(uint32_t sequence, char **out_text);
void subghz_receiver_discard(void);
void subghz_receiver_stop(void);
void subghz_receiver_finish(void);
bool subghz_receiver_is_running(void);
esp_err_t subghz_receiver_get_error(void);
```
- `freq = 0` enables frequency hopping mode.
- Configured starts validate and copy the radio settings before worker creation;
  the caller can release or change its copy. The legacy entry point loads defaults.
- Returns `ESP_OK` on success, `ESP_ERR_INVALID_STATE` if already running, `ESP_ERR_NO_MEM` on task creation failure.
- Task stack: 8192 bytes, `SYS_PRIO_SERVICE_HI`, `SYS_CORE_RADIO`.

Shutdown is asynchronous: `subghz_receiver_stop()` requests cancellation and
`subghz_receiver_is_running()` remains true through startup and hardware cleanup.
The worker owns the resource grant until RMT disable, channel deletion and confirmed radio IDLE succeed.
Failed cleanup retries while retaining the DMA buffer, callback queue and radio
grant; callers cannot start another RX worker during this interval. A cancellation
before the worker starts is preserved. Queue/result-mutex allocation failures
abort without publishing an unusable receiver. The decoding buffer uses explicit
PSRAM; the aligned DMA capture buffer remains internal. The worker stops on radio
initialization, tuning, RX entry or RMT errors and exposes the first failure through
`subghz_receiver_get_error()`. A new accepted start resets this error. Before a hop,
RMT is disabled and old-channel queue events are discarded; the frequency is
published only after successful tuning/RX entry, followed by fresh RMT capture.

`python3 tools/highboy/test_subghz_lifecycle.py` exercises the production worker
with explicit hardware/RTOS fakes and Address/UndefinedBehaviorSanitizer. It covers
startup cancellation, competing starts, allocation/setup/radio errors, old-channel
queue draining, and repeated disable/delete/IDLE failures with a late callback. It does not prove physical RF or
DMA behavior on the board.

### Transmitter (`subghz_transmitter`)

Asynchronous queue-based transmitter using CC1101 GDO0 in async mode.
Each queued frame owns a copy of timings **and frequency/preset/custom radio settings**. Radio configuration
occurs in the worker with RMT disabled; both GDO outputs are high impedance before
RMT resumes. Decoded/text preparation retains its 64 KiB / 8192-pulse bounds.
Saved and pending RAW files use a bounded 64 KiB PSRAM symbol ring, so file replay
has no total pulse/file-duration limit. The TX worker validates the complete file
before RF starts, with cancellation checks and periodic yields; the UI reads only
the bounded header. The RMT simple encoder consumes prefilled symbols without SD
I/O in the ISR. The worker refills while RMT runs, splitting long pulses into
equal-level halves rather than truncating at 32767 us. Held sends rewind at EOF,
including across odd pulse counts, without inserting a block boundary or retuning.
If SD cannot keep up, an underrun stops transmission with an error instead of
silently padding or dropping pulses. An unfinished TX retains its file, encoder
and ring until channel cleanup succeeds. Release/Back cancels as before.
Replay text, timings and symbols explicitly use PSRAM. The P4 profile keeps the
RMT ISR cache-safe mode disabled; enabling that SDK option requires revisiting
symbol storage/encoder memory. Allocation failure does not fall back to internal RAM.
Stop is checked between bounded waits (50 ms for memory waveforms, 1 ms for RAW
stream refill) and during file validation; buffers remain owned until cleanup succeeds. Native replay requests +10 dBm nominal; this is not measured EIRP.

**Flow:** `subghz_tx_send_raw()` -> FreeRTOS Queue -> TX Task -> RMT TX -> CC1101

#### API

```c
esp_err_t subghz_tx_init(void);
void subghz_tx_stop(void);
void subghz_tx_get_status(subghz_tx_status_t *out_status);
esp_err_t subghz_tx_send_raw(const int32_t *timings, size_t count);
esp_err_t subghz_tx_send_configured(const int32_t *timings, size_t count,
                                    const subghz_tx_config_t *config);
```
- `subghz_tx_init` returns `ESP_OK` on success, `ESP_ERR_NO_MEM` on queue creation failure.
- `subghz_tx_send_raw` returns `ESP_OK` when queued, `ESP_ERR_INVALID_STATE` if not running, `ESP_ERR_INVALID_ARG` for invalid pulses, `ESP_ERR_NO_MEM` on allocation failure, or `ESP_ERR_TIMEOUT` if the queue is full.
- Queue depth: 10 items. Rejects new frames if full; zero-length pulses and total durations above 120 seconds are rejected before copying.
- Timing data is copied internally; caller retains ownership of the original buffer.
- RMT half-symbol limit: 32767 us; longer pulses are split without changing level.
- Legacy raw jobs preserve radio settings applied after init (including diagnostic PA/rate); configured calls retain file metadata.
- Read/Read RAW now default to fixed 433.92 MHz / OOK270. Config can enable hopping.
- Task stack: 4096 bytes, `SYS_PRIO_SERVICE_HI`, `SYS_CORE_RADIO`.

The native Send, Saved and Read-result actions open the same transmit detail screen.
Opening it prepares the capture once without transmitting. The filename, frequency,
protocol and decoded key (or RAW pulse count) remain visible. Pressing OK starts
PTT immediately. Its queued configuration sets `repeat_until_stopped`, so the
TX worker reuses the same PSRAM waveform and configured radio for each replay
while held. It does not wait for the 50 ms UI timer, allocate a new waveform,
reset the CC1101 or recalibrate between repeats. RMT submission still has a
scheduler-dependent gap; this is not hardware gapless looping. Legacy/API
one-shot requests leave this flag false. No LONG_PRESS event is needed. Decoded files default
to 10 contiguous Princeton frames or 5 frames for other encodable protocols; a
checked `Repeat: 1..100` overrides that count. RAW waveforms play once per burst.
Releasing OK cancels the active burst and all repeats, including short taps. BACK and
screen close stop owned TX and free the prepared PSRAM pulses. There is at most one
UI burst outstanding. Repeats reuse the encoded waveform, including its original
key/counter; they do not advance rolling-code state. Unsupported encoders remain
visible as errors with BACK available. Held transmission remains busy until release; it does not report each worker
repeat as a separately completed UI job. Intentional cancellation does not emit an error alert.
Native TX owns `LED_ACTIVITY_SUBGHZ` from TX entry through confirmed radio IDLE;
its nonblocking info-color pulses use the user's brightness and notification priority.

`subghz_replay_load()` prepares a zero-initialized `subghz_replay_t` (free a previous
capture before loading); `subghz_replay_send()` queues a copy; `subghz_replay_free()`
releases the prepared buffer. `subghz_replay_file()` remains a one-shot wrapper.

### Brute Force (`subghz_brute`)

The existing menu runs a bounded CAME 12-bit sweep at 433.92 MHz. The worker copies
Radio Config's modem settings at start and submits configured, one-shot jobs;
it does not retune the driver from the UI task. Each code must finish successfully
before it is counted or the next code is queued. The existing 100 ms pacing is
retained after completion. Failed encoding, allocation or transmission stops the
sweep and publishes its error and actual completed count, rather than reporting
success. The 2048-pulse working buffer is explicitly allocated in PSRAM.

Cancellation is checked while waiting and pacing. The worker stops TX and releases
its buffer on every exit; a new sweep cannot start while the previous worker is
still exiting. Receiver shutdown is polled with a bounded deadline before TX starts.
The screen close hook and delete fallback cancel the worker and timers, including
programmatic navigation; timer creation failure prevents transmission. The screen
preserves partial/error counts and adapts its scope/readout to portrait/landscape.
This is an exhaustive code sweep, not a detector of successful remote operation.

### Spectrum Analyzer (`subghz_spectrum`)

Sweeps across a frequency span by stepping the CC1101 through discrete frequencies and reading RSSI values. Produces 80-sample spectral lines.

The menu's Frequency Analyzer now uses the dedicated
[frequency_analyzer service](../frequency_analyzer/README.md): Auto/common channels,
band selection, wide/fine RSSI refinement, threshold, history and Read RAW handoff.
Its discovery bandwidth/AGC is independent of Radio Config; the RAW handoff retains
that configured demodulation profile and changes only frequency. The spectrum API
below remains available to console/apps. Its configured variant accepts a copied
radio profile; the legacy two-argument API retains its OOK800 profile.

**Sweep Process:**
1. Divides the span into 80 frequency steps
2. For each step: tune CC1101, wait 400 us stabilization, take 3 RSSI peak samples
3. Updates a mutex-protected global `subghz_spectrum_line_t` structure

#### Data Structure

```c
typedef struct {
  uint32_t center_freq;
  uint32_t span_hz;
  uint32_t start_freq;
  uint32_t step_hz;
  float dbm_values[SPECTRUM_SAMPLES];
  uint64_t timestamp;
} subghz_spectrum_line_t;
```

#### API

```c
esp_err_t subghz_spectrum_start(uint32_t center_freq, uint32_t span_hz);
esp_err_t subghz_spectrum_start_configured(uint32_t center_freq, uint32_t span_hz,
                                        const cc1101_radio_config_t *radio);
void subghz_spectrum_stop(void);
esp_err_t subghz_spectrum_get_error(void);
bool subghz_spectrum_get_line(subghz_spectrum_line_t *out_line);
```
- Task stack: 4096 bytes, `SYS_CORE_RADIO`. Priority comes from `sys_prio.h`: the `SYS_PRIO_BACKGROUND_LO` band (lowest non-idle background work), so the sweep never contends with the renderer or latency-sensitive services.
- Thread-safe reads via `subghz_spectrum_get_line`.
- Start rejects zero steps, unsigned overflow and unsupported frequencies at any
  sampled bin before acquiring the radio. Busy/allocation/task failures are returned.
- Failed tuning, RX entry or RSSI reads stop the sweep without publishing partial
  data. `subghz_spectrum_get_error()` retains the first worker error until the next
  accepted start; a radio read failure cannot become a plausible RSSI sample.

### Signal Analyzer (`subghz_analyzer`)

Analyzes unknown signals by building a pulse duration histogram to estimate modulation parameters and recover bitstreams.

**Analysis Steps:**
1. **Histogram:** Builds 50 us bins (up to 5000 us) from absolute pulse durations
2. **TE Estimation:** First significant histogram peak = estimated Time Element
3. **Modulation Heuristic:** 2 peaks = Manchester/Biphase, 3+ peaks = PWM/Tri-state
4. **Bitstream Recovery:** Slices pulses into TE-sized bits using edge-to-edge detection

#### Data Structure

```c
typedef struct {
  uint32_t estimated_te;
  uint32_t pulse_min;
  uint32_t pulse_max;
  size_t pulse_count;
  const char *modulation_hint;
  uint8_t bitstream[128];
  size_t bitstream_len;
} subghz_analyzer_result_t;
```

#### API

```c
bool subghz_analyzer_process(const int32_t *pulses, size_t count, subghz_analyzer_result_t *out_result);
```
- Requires minimum 10 pulses. Filters durations < 50 us as noise.

### Protocol Serializer (`subghz_protocol_serializer`)

Serializes and parses `.sub` file format for decoded and raw signals.

**File Format:**
```
Filetype: High Boy SubGhz File
Version 1
Frequency: 433920000
Preset: 6
Protocol: Princeton
Bit: 24
Key: 00 00 00 00 XX XX XX XX
TE: 350
```

RAW variant replaces Protocol/Bit/Key/TE with:
```
Protocol: RAW
RAW_Data: 350 -700 350 -350 700 -350 ...
```

Customized captures add an optional line before `Protocol`:
`Radio: <MDMCFG4> <MDMCFG3> <DEVIATN> <AGCCTRL2>` (four decimal bytes).
These preserve bandwidth/rate, deviation and gain limits exactly. Remaining modem
registers come from the numeric base preset. Default captures omit this extension;
older files without it load preset defaults. Both native and bounded app replay
validate and apply it. Duplicate, incomplete, out-of-range or unsupported values
fail before TX. Serialization reads the active radio configuration and fails if
it cannot obtain a complete snapshot; it does not silently substitute menu values.
This is a High Boy file extension, not Flipper's custom-preset file format.

#### API

```c
uint8_t subghz_protocol_get_preset_id(void);
size_t subghz_protocol_serialize_decoded(const subghz_data_t *data, uint32_t frequency, uint32_t te, char *out_buf, size_t out_size);
size_t subghz_protocol_serialize_raw(const int32_t *pulses, size_t count, uint32_t frequency, char *out_buf, size_t out_size);
size_t subghz_protocol_parse_raw(const char *content, int32_t *out_pulses, size_t max_count, uint32_t *out_frequency, uint8_t *out_preset);
esp_err_t subghz_protocol_parse_radio(const char *content, cc1101_preset_t preset,
                                    cc1101_radio_config_t *out_config);
```


#### Selected lab/rf integration

Reviewed `origin/lab/rf` at `edc3182a`, with protocol/replay corrections from
`37709da5`, driver corrections from `9c84ed6e` and RX improvements from `5be71db9`.
Princeton requires a complete 24-bit frame and stop, retaining measured TE and
guard time. Nice Flo rejects longer PWM prefixes; RCSwitch requires a closing
sync and retains measured TE. Flipper standard preset names and multiple
`RAW_Data:` lines are accepted; malformed metadata and oversized expanded bursts
are rejected before TX. The local `Radio:` overrides remain supported.

The local deferred burst analysis, RSSI history and PTT UI remain. The lab
receiver's fixed 315 MHz default and immediate per-block decoding are not imported.
The dedicated frequency-discovery service/UI from `ed490290` replaces the old
menu spectrum view, with checked per-point SPI batching and a 2 ms RSSI settle.
Lab-only bench/chamber code and build/config reductions remain excluded.
This integration does not claim faster physical scans or measured range.

Host tests cover independent pulse vectors, truncated-frame rejection, measured
replay timing, contiguous repeats, named presets, multiline RAW and existing
custom-radio/PTT/lifecycle behavior. Physical RX/TX acceptance remains pending.

Replay waits for RX cleanup in 10 ms steps, up to one second, and refuses TX if
RX still owns the radio. It no longer relies on an unconditional 150 ms sleep.

### Storage (`subghz_storage`)

Writes `.sub` captures to SD using checked write/close operations. Exclusive file
creation prevents overwriting a previous capture; explicit saves do not replace existing
files across reboots. Listing scans the directory and retains the newest entries
within the caller's capacity, ordered by modification time and filename on ties.
Both decoded and RAW serialization buffers use explicit PSRAM allocation; the RAW
text buffer is 64 KiB to accommodate the bounded Read collector. Save, read and
delete reject path separators, hidden/relative stems and oversized names. Directory,
read and close errors propagate. Bounded reads remain suitable for header previews;
Decoded replay rejects a full/truncated text buffer; RAW replay uses the streaming
reader instead of this preview API. Saved ignores OK on an empty
list; deferred deletion callbacks cannot reopen a screen after its owner has closed.

#### API

```c
esp_err_t subghz_storage_init(void);
esp_err_t subghz_storage_save_decoded(const char *name, const subghz_data_t *data, uint32_t frequency, uint32_t te);
esp_err_t subghz_storage_save_raw(const char *name, const int32_t *pulses, size_t count, uint32_t frequency);
```
- Returns `ESP_OK` on success, `ESP_ERR_INVALID_ARG` on null arguments, `ESP_ERR_NO_MEM` on allocation failure.

## Software review coverage

All native menu entries are included in the host validation scope:

| Entry | Coverage |
| --- | --- |
| Read | Burst collection, decoding, hopping ownership, cancellation and explicit Save |
| Read RAW | Continuous SD streaming, manual finish, overflow/write errors, explicit Save and temporary-file ownership |
| Frequency Analyzer / Waterfall | Scan plans, boundaries, injected radio failures, pause/stop, PSRAM cleanup and themed layouts |
| Brute Force | Encoding/TX/allocation failures, actual completion counts, cancellation, reentry and UI layout |
| Saved | Exclusive writes, newest-first list, name validation, empty-list guard and deferred owner checks |
| Send | Exact imported RAW pulses, stored presets, held repeats, release/Back and error cleanup |
| Radio Config | Preset/override reload, all carrier selections, invalid-band rejection and both orientations |

`tools/subghz_api/test_subghz_api.py` includes the production sweep worker with
host fakes. `tools/highboy/tests/test_subghz_brute_ui.py <host-ui-dir>` validates
the production LVGL sweep screen, alongside the existing Read, Send and preset
screen tests. Analyzer/waterfall worker and UI tests remain in
`tools/frequency_analyzer/`. These checks do not establish physical range, output
power, sensitivity, gap-free RAW acquisition or superiority to Flipper hardware.

## Protocol Plugins (`protocols/`)

The protocol system follows a **plugin architecture**. Each protocol is a self-contained module (e.g., `protocol_princeton.c`) that implements a common interface and is registered in a central registry. This design allows adding support for new protocols without modifying existing code - just create a new `protocol_*.c` file, implement the `subghz_protocol_t` interface, and register it in `subghz_protocol_registry.c`.

### Plugin Interface

Every protocol plugin must export a `subghz_protocol_t` struct with two function pointers:

```c
typedef struct {
  const char *name;
  bool (*decode)(const int32_t *pulses, size_t count, subghz_data_t *out_data);
  size_t (*encode)(const subghz_data_t *data, int32_t *pulses, size_t max_count);
} subghz_protocol_t;
```

- **`decode`**: Receives raw pulse timings and attempts to recognize the protocol. Returns `true` if the signal matches, filling `out_data` with serial, button, bit count, and raw value.
- **`encode`**: Converts structured data back into pulse timings for retransmission.

### How It Works

1. Each plugin file declares a global `subghz_protocol_t` (e.g., `protocol_princeton`)
2. The registry (`subghz_protocol_registry.c`) holds an array of pointers to all registered plugins
3. On signal reception, `subghz_protocol_registry_decode_all()` iterates through all plugins in order, calling each `decode()` until one claims the signal
4. If no plugin matches, the signal falls through to the `subghz_analyzer` for heuristic analysis

### Adding a New Protocol Plugin

1. Create `protocols/protocol_mydevice.c`
2. Implement `decode()` and optionally `encode()`
3. Export: `subghz_protocol_t protocol_mydevice = { .name = "MyDevice", .decode = ..., .encode = ... };`
4. Register in `subghz_protocol_registry.c`:
   - Add `extern subghz_protocol_t protocol_mydevice;`
   - Add `&protocol_mydevice` to the `s_protocols[]` array

### Registered Plugins

| Plugin       | Modulation | Typical Use                  |
|--------------|------------|------------------------------|
| RCSwitch     | OOK/PWM    | Generic remote switches      |
| Princeton    | OOK/PWM    | Fixed-code remotes           |
| CAME         | OOK/PWM    | Gate/garage remotes          |
| Nice FLO     | OOK/PWM    | Gate/garage remotes          |
| Ansonic      | OOK/PWM    | Gate remotes                 |
| Chamberlain  | OOK/PWM    | Garage door openers          |
| Holtek       | OOK/PWM    | Remote controls              |
| LiftMaster   | OOK/PWM    | Garage door openers          |
| Linear       | OOK/PWM    | Gate/access control          |
| KeeLoq       | OOK/PWM    | HCS301 rolling-code remotes   |
| GateTX       | OOK/PWM    | Gate remotes (24-bit)        |
| Holtek_HT12X | OOK/PWM    | HT12E DIP remotes (12-bit)   |

RCSwitch, CAME and the fixed-code plugins (Ansonic, Chamberlain, Holtek,
LiftMaster, Linear, Nice FLO, Princeton) also transmit: each has a real `.encode`
(shared `subghz_pwm_encode` helper), verified by `subghz selftest`. KeeLoq is the
rolling-code plugin (see the KeeLoq section below); it replaced the old Rossi stub.

The HighBoy merge retains `protocol_rossi.c` as unregistered legacy source only.
It is not a usable decoder: its broad pulse heuristic fabricated the serial
`0xDEADBEEF` and returned no captured value. It must not shadow the KeeLoq decoder.
The existing custom UI and PSRAM layout are unchanged by this codec merge.

Registry lookup accepts an exact name first, then a leading registered name
followed by a space or `(`. This preserves saved names such as `CAME 12bit`,
`Nice Flo 12bit`, `RCSwitch(P1)` and `KeeLoq <manufacturer>` without accepting
arbitrary substring matches. Replay uses that same registry encoder.
RCSwitch rejects zero or wider-than-32-bit input and out-of-range protocol
indices before any shift or index adjustment.

### Utility Functions (`subghz_protocol_utils.h`)

```c
uint32_t subghz_abs_diff(uint32_t a, uint32_t b);
bool subghz_check_pulse(int32_t raw_len, uint32_t target_len, uint8_t tolerance_pct);
```
Helper functions available to all plugins for pulse timing validation with percentage-based tolerance.

### Registry API

```c
void subghz_protocol_registry_init(void);
bool subghz_protocol_registry_decode_all(const int32_t *pulses, size_t count, subghz_data_t *out_data);
const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name);
```

## KeeLoq Rolling Code (`subghz_keeloq`)

HCS301 rolling-code support, surfaced as the `KeeLoq` protocol plugin (which
replaced the old Rossi stub). Manufacturer keys load at runtime from the SD asset
`config/subghz/keeloq_mfcodes.txt` (Flipper mfcodes format, 116 keys).

- **Decode (capture):** `subghz_keeloq_identify(fix, hop, ...)` brute-forces every
  loaded manufacturer key: derives the per-device key, decrypts the 32-bit hop, and
  on a discrimination + button match recovers serial / button / counter / maker.
  The plugin reports it as `KeeLoq <manufacturer>` with the serial and counter in
  `subghz_data_t`.
- **Encode (replay):** `subghz_keeloq_encode(...)` transmits the NEXT code
  (counter + 1) — the rolling-code prediction attack — reached from the normal
  replay path (`subghz_replay_file` → registry encode).
- **Key derivation:** ported verbatim from Momentum firmware (GPLv3,
  `lib/subghz/protocols/keeloq_common.c`). Deterministic schemes — SIMPLE, NORMAL,
  MAGIC_XOR, MAGIC_SERIAL 1/2/3, PUJOL, SIMPLE_JCM — support decode **and** rolling
  TX; AERF is decode-only (derived-key decrypt, no encrypt inverse). Adding
  UNKNOWN (multi-scheme brute: simple / normal / byte-reversed key) and SECURE
  (serial-derived or zero seed fallback) brings decode coverage to ~110 of the 116
  keys. The remaining ~6 are separate protocols upstream — FAAC SLH, KINGGATES,
  JAROLIFT — plus ERREKA (needs the seed) and the reserved Beninca_ARC; those need
  their own frame decoders ported and a real capture to validate.
- **Framing:** the 64-bit key is transmitted LSB-first, so decode bit-reverses it
  to cipher order (`subghz_keeloq_decode_key`); the discrimination is the low
  **8 bits** of the fixed part (HCS200/HCS300 common), not 10.
- **Self-test:** `subghz selftest` runs the cipher inverse (checked against a
  published KeeLoq test vector), a full encode→PWM→decode round-trip, and three
  **real golden captures** from `FlipperZero-Subghz-DB` (DoorHan, Elmes_Poland,
  JCM_Tech) that must decode to the right maker + counter. The cipher, disc,
  reversal and learnings are validated against real remotes this way; only the
  on-air RF (does the transmitted signal open a real gate) still needs an SDR.

The decoded save format carries extra `Serial:` and `Btn:` lines so a saved KeeLoq
capture replays with the correct rolling counter.

Legacy fixed-code files without these fields remain readable: serial falls back
to the stored key value and button defaults to zero. Replay accepts the full
40-byte protocol-label buffer used by the KeeLoq adapter; longer labels are
rejected rather than truncated. Decoded serialization returns zero and clears
the output on insufficient buffer space, never a length beyond the buffer.

### Offline merge regression tests

Run `python3 -B tools/highboy/test_subghz.py` from the repository root. It compiles
the actual protocol, registry, serializer, replay and KeeLoq sources with strict
C11 warnings and UndefinedBehaviorSanitizer. Radio, FreeRTOS and storage calls
are RAM-only test doubles; it opens no device, SD assets, or private key files.

Coverage includes all 12 fixed-code self-test vectors, an independent expected
PWM pulse vector, exact/decorated name lookup, invalid/small buffers, decoded and
raw file replay, legacy files, cipher inverse/frame round-trips, and a synthetic
long manufacturer name through serialization and replay. The synthetic key is
a public test constant, not a key read from a device. Golden manufacturer-capture
identification is skipped without the real public asset and is not claimed by
this host test. Neither host tests nor `subghz selftest` transmit RF or establish
electrical, modulation-tolerance, or real-device interoperability.

## TODO / Not Yet Supported

Tracked against `FlipperZero-Subghz-DB` (validation vectors) and Momentum firmware
(reference decoders). Ordered by how blocked each item is.

**Portable + validatable (do next).** Fixed-code protocols present in the DB with
plenty of captures, but with a non-PWM encoding so they need a custom decoder (not
the shared `subghz_pwm_encode` helper):
- **MegaCode** — pulse-position (te_short == te_long, fixed slots), 24-bit. ~10 DB captures.
- **SMC5326** — tri-state (0/1/float), te 300/900, 25-bit. ~5 DB captures.
- **Firefly (Allstar)** — long inter-bit gap (te 600/4000), 18-bit. ~3 DB captures.

**Separate rolling-code protocols (own frame + crypto).** Need their own decoder
ported from Momentum:
- **Security+ 2.0** (Chamberlain rolling) — ~4 DB captures.
- **Somfy Telis (RTS)** — 1 DB capture.

**KeeLoq makers needing a separate protocol decoder.** These are NOT KeeLoq
learning types — Momentum implements them as standalone protocols (different frame).
There are **zero captures in this DB**, so a port could not be validated yet:
- **FAAC SLH**, **KINGGATES (Stylo 4k)**, **JAROLIFT**.

**KeeLoq makers needing data we do not have:**
- **AN-Motors** — uses standard KeeLoq, but its manufacturer key is not in our
  `keeloq_mfcodes.txt`; the `.sub` is encrypted with that key, so it cannot be
  recovered from the capture. Need the key.
- **Beninca_ARC** — reserved even upstream (Momentum leaves type 9 unimplemented).
- **ERREKA** — needs the seed carried in an extended frame we do not parse.

**Cross-cutting:** over-the-air RF validation (does a transmitted frame open a real
gate; does the pwm bit order match a real remote) still needs an SDR / real target.
New fixed protocols are validated only by encode↔decode round-trip today.

## Common Types (`subghz_types.h`)

```c
typedef struct {
  const char *protocol_name;
  uint32_t serial;
  uint8_t btn;
  uint8_t bit_count;
  uint32_t raw_value;
} subghz_data_t;
```

Shared data structure used across decoder, serializer, storage, and UI layers.

## App capture observations

ABI 1.12 publishes read-only `subghz.rx_poll` for signed apps and JS/Lua,
requiring RX permission. Each app/run has a private cursor over the latest
native capture snapshot; sequence IDs persist across receiver restarts.
Polling never starts or stops the receiver and does not expose raw timings.
ABI 1.13 adds the bounded active operations below; asynchronous session and
spectrum entries remain NULL.
See the [ABI contract](../app_sdk/README.md#native-radio-observations-abi-112).

## Native radio worker ownership

Native TX now creates RMT hardware inside its worker and reports startup through
a per-call signal. Stop waits up to 2500 ms for the worker; a timeout leaves the
grant busy rather than freeing live RMT state. The worker waits for in-flight
queue senders, retains its current symbols through transmit/cleanup failure,
and retries disable/channel-delete/encoder-delete and radio IDLE until successful. Only then
are pending timings, queue and grant released. Concurrent reinitialization is
rejected while starting/draining. Pulse conversion handles `INT32_MIN` without
signed overflow; pulses exceeding the bounded total duration are rejected.

`subghz_tx_get_status()` reports queued plus active jobs in `is_busy` and retains
the first worker error in `error`. A job completes only after RMT finishes and the
radio confirms IDLE. Configuration/SPI/RMT failures halt the worker and cannot be
reported as success merely because enqueue succeeded. The status error clears on
a new accepted initialization. Native RX/TX/spectrum worker failures emit one
error log/LED signal per operation; screens also display the failed operation.

Native spectrum now reserves `RES_SUBGHZ`, publishes its running state before
spawning the task and preserves startup cancellation. Failed mutex/task setup
releases the grant. A cancelled partial sweep is discarded instead of publishing
uninitialized bins; the grant is released by the worker after its final radio
IDLE confirmation. UI/companion callers can encounter a busy radio and must retry after
shutdown. These changes preserve the existing pins, presets and task priorities.

## Bounded app RX/TX (ABI 1.13)

`subghz.receive` performs one capture with an explicit frequency, preset and
1–1000 ms capture wait. `subghz.tx_raw` sends a bounded OOK pulse train and waits
for completion. Signed apps and JS/Lua use the same capability-gated tables and
persistent RF worker. These functions do not start the native continuous RX/TX
workers or create app-owned background sessions. Captures are returned directly;
they are not auto-saved or published into the UI's latest-capture snapshot.
See the [ABI bounds and script examples](../app_sdk/README.md#bounded-sub-ghz-api-abi-113).

`python3 tools/subghz_api/test_subghz_api.py` compiles production backend, lease,
driver wrappers, native TX and spectrum code with ASan/UBSan and explicit
RMT/SPI/RTOS fixtures. Native TX tests use a real host worker thread. No RF is
received or transmitted by these tests; real frequency/timing/teardown acceptance
remains under #156. RX capture timeout and TX wait limits exclude driver setup,
bus-lock waits and recovery; they are not hard deadlines for the complete call.


## Bounded app spectrum (ABI 1.15)

`spectrum_capture` performs a complete 80-bin RSSI sweep through the existing
persistent app RF worker. It requires RX permission and an entire span inside
one supported CC1101 band, reserves `RES_SUBGHZ` without preemption, and yields
between bins. Native RX/TX/spectrum and other radio owners block acquisition.
Any SPI or cleanup error discards the whole result; retained cleanup keeps the
radio reserved. It does not start or overwrite the continuous native spectrum
worker or its UI snapshot. ABI 1.21 adds the asynchronous session interfaces described below. Bounds, sampling and result layout are specified in the
[ABI contract](../app_sdk/README.md#bounded-sub-ghz-spectrum-abi-115).

The production backend host tests cover exact tuning/sample order, peak RSSI,
minimum/maximum/odd spans, all band edges, invalid and overflowing requests,
permissions/cancellation, contention, partial SPI failure and retained idle/power
cleanup. These explicit SPI/RTOS fixtures do not validate RF on the device.


## Bounded shared raw capture

ABI 1.18 publishes the most recent successful bounded app-worker receive as
`subghz.get_raw`; JavaScript and Lua use `tos.call("subghz.get_raw", max)`.
This global snapshot is independent of native asynchronous UI RX history.
Failed capture or hardware cleanup preserves the previous complete capture.
The [ABI contract](../app_sdk/README.md#sub-ghz-raw-snapshot-abi-118) defines
capacity, permissions and short-buffer behavior.

RAW serialization/parsing rejects truncation: insufficient output capacity
returns zero rather than a shorter successful pulse train. Parsing requires
unique line-anchored frequency/preset fields, checks numeric range, rejects
zero/overflow/malformed timings and rejects additional pulses beyond capacity.
Frequency/preset outputs are published only after complete success; discard
pulse output on error. The saved metadata remains subject to radio validation
before transmission.


## Shared file replay parser

`subghz_protocol_parse_capture` parses RAW or decoded files without touching the
radio. The native replay UI and ABI 1.19 use this same parser and native encoder
registry. Complete metadata is validated before stopping the native receiver or
starting TX; malformed fields and over-capacity pulses fail. Legacy fixed-code
files may omit Serial/Btn/Preset (key-derived serial, button zero, OOK 800 kHz).
Decoded keys with nonzero upper four bytes are rejected instead of truncated.
Native storage replay rejects a full 65535-byte read because that API cannot prove
the file was complete; embedded NUL is rejected too. App replay independently
checks the complete file up to 8192 bytes and bounds the resulting transmission.
See the [ABI contract](../app_sdk/README.md#sub-ghz-file-replay-abi-119) for path,
frequency, duration, capability and hardware ownership requirements.


## Owned app sessions (ABI 1.21)

All published `api->subghz` entries are implemented. `rx_start` and
`spectrum_start` create one task-owned background session. This uses the bounded
app backend rather than the native UI receiver, which exposes pending captures for an explicit UI Save action. There is one global
app session, a persistent 4096-byte service worker on `SYS_CORE_RADIO` at
`SYS_PRIO_SERVICE_LO`, and one latest complete copied result.

RX repeats 100 ms receive windows; spectrum repeats full 80-bin sweeps. Each
iteration uses the persistent RF worker, exclusive CC1101 lease, checked power
hold and normal RMT teardown, then yields for 10 ms. Setup/recovery/cleanup add
time: this is asynchronous **windowed** capture with gaps, not a gap-free RF
receiver or a sample-rate guarantee. No capture is automatically saved to SD
or injected into the native UI's history. Native UI/companion services remain
independent and share physical ownership through the existing lease.

Startup waits for the first completed window/sweep or an actual setup error
(up to 3000 ms to observe readiness). An empty RX window is normal only if its
cleanup completed; a SPI/setup/cleanup timeout fails the session. Error results
remain negative even for ESP_FAIL. Failed startup returns session zero and
requests background cleanup. Timeout does not assert that the hardware stopped.

Only the originating task can retune, stop or poll its session. IDs are never
reused before reboot; exhaustion fails. `set_frequency` applies to the next RX
window and discards the pending old-frequency record. Spectrum must be stopped
and restarted to change its range. `rx_poll` observes the owned session when
one exists; other callers retain the previous native UI observation contract.
`get_raw` remains the global last successful app-backend capture, now including
background windows. It is not a per-app history or an atomic pair with rx_poll.

`session_stop` works even when the caller's should_stop flag is set. It waits up
to 3000 ms for cleanup, without holding a mutex across that wait. A timeout
retains the worker, lease and buffers; the firmware retries cleanup. While a
background session is starting/running/draining, bounded app receive/TX/replay/
sweep calls fail busy. Poll and raw-copy calls remain available. A native radio
owner winning a gap terminates the app session with an error; terminal failures
set resource_lost, and a successful new startup clears it.

Binding/rebinding/unbinding a task cancels its previous session. This covers
natural exit, script completion/deadline and forced app teardown through the
existing context paths. Cancellation is nonblocking: the app/ELF/context can
be unloaded because the firmware owns all pending requests, callbacks, snapshots
and hardware buffers. The stored task handle is an identity value, never a
pointer dereferenced after teardown. Failed hardware cleanup prevents reuse
until the persistent RF worker confirms release. Binding teardown acquires the
session lock before the binding lock; terminal-error notification uses that
same order, and callbacks must remain nonblocking.

See the [native contract](../app_sdk/README.md#owned-sub-ghz-sessions-abi-121) and
[script calls](../scripts/README.md#sub-ghz-session-bindings-abi-121).
The production backend tests run the actual service task on a host thread and
cover RX/spectrum start, retune, short buffers, startup RF-gate starvation/busy
radio, foreign/stale owners, cancellation, generic/SPI errors, retained teardown
and ID exhaustion. RMT, SPI and scheduler calls remain explicit fixtures; they
do not establish RF timing, device stack headroom or reception quality.

## Local RAW replay verification

`python3 tools/highboy/test_subghz.py --capture /path/to/capture.sub` also loads
an external Flipper RAW file through the production replay parser. An independent
host reader checks every signed duration, ordering, frequency and named preset
against the queued transmission. No radio is accessed or RF emitted. The file is
read from its supplied path and is not included in the repository.

On 28 November 2026, `Tesla_433_AM270.sub` (SHA-256
`c73e8b405b8136c5987abc0c304611fbc5839a941f2b2ffbf1a75ab6e76ec98d`)
passed: 10,991 bytes, five RAW_Data lines, 2,395 pulses totaling 1,421,522 us,
433,920,000 Hz, AM270. The parser preserves the entire waveform; this does not
validate antenna power, on-air timing or range. The longest pulse is 32,700 us,
below the TX half-symbol limit. Native configured replay still requests the
existing nominal +10 dBm; it has not been increased.

The local Momentum/Flipper reference starts decoded TX on OK press and stops on
release (`applications/main/subghz/views/transmitter.c`). Its RAW view ends the
repeat request on release while letting the current file finish; Highboy retains
its requested immediate PTT cancellation for both RAW and decoded files. RAW
recording in the reference streams batches of 512 pulses to a temporary file.
Highboy now follows that storage model, with explicit Save and bounded PSRAM
rings for RX and TX. The hardware acquisition and stop timing remain different;
this is not a claim of byte-for-byte identical peripheral behavior.

Host validation covers 131072 file pulses with exact signed duration/order and
rewind, more than 1.3 million simulated RX pulses beyond the old time/block bounds,
DMA buffer reuse, overflow, SD write/close errors, discard and retained ownership.
The production TX ring encoder is checked beyond 131072 pulses, including wrap,
odd tails, held repeats and underrun. Read/Send UI tests cover both orientations
and light/dark themes. Sustained RF capture and SD latency on hardware are not
established by these host tests.

### Dictionary memory placement

The bounded KeeLoq manufacturer dictionary is CPU-only external BSS in PSRAM.
Loading and decoding do not reserve its 7.5 KiB table in internal SRAM. Radio
ISR/DMA buffers retain their separately documented capability requirements.
