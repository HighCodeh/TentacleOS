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

# IR TX/RX Service

## V2.03 ownership

The P4 owns codecs, AC state encoders, files, UI, permissions and the public
`ir.h` API. `firmware_c5/components/Service/ir_remote` owns the physical RMT
channels and emitter. P4 GPIO0/1 are C5 reset/boot controls and are never used
for IR. Update both applications for this backend; an older C5 returns an
unsupported-command error. Protocol v2 and transport envelope revision 1 remain
unchanged; the IR extension has its own revision 1.

| Connection | V2.03 owner |
| --- | --- |
| Emitter / T7 gate | C5 GPIO6, active high, idle low |
| TSOP75338TR receiver | C5 GPIO10, active low |
| Optical mark on the wire and in raw API | level 1 |

RX uses RMT input inversion to normalize the receiver polarity. The receiver's
active-low output is specified in [Vishay document 82495, figure 1](https://www.vishay.com/docs/82495/tsop753.pdf).
The schematic connects its output directly to C5 GPIO10. TX uses 33% carrier
duty, 1 MHz duration resolution and ends low. Carrier 0 disables modulation;
other carriers must be 10–100 kHz. Optical sensitivity of the 38 kHz receiver is
not a guarantee that every carrier in the encoder library can be captured.

## SPI contract and lifecycle

`SPI_ID_IR_REMOTE` (`0x0011`) carries the internal backend RPC. Existing
`SPI_CAT_IR` companion operations remain P4-local and are not forwarded verbatim.
All scalar fields are fixed-width little-endian. Neither chip copies an RMT
bitfield struct onto the wire.

Request header (24 bytes): `epoch:u64`, `operation:u32`, `action:u8`,
`revision:u8`, `count:u16`, `offset:u16`, `reserved:u16`, `value:u32`.
Reply header (24 bytes): `epoch:u64`, `operation:u32`, `result:i32`, `state:u8`,
`revision:u8`, `count:u16`, `offset:u16`, `reserved:u16`.
Reserved fields must be zero. SPI status reports request admission; `result`
reports asynchronous operation completion using `esp_err_t`.

Each serialized symbol is two little-endian u16 pulses. Bits 0–14 hold duration
in microseconds; bit 15 holds the level. Up to 56 symbols fit in one chunk.
Offsets/counts are in symbols. A zero second duration is permitted only on the
last TX symbol; a zero first duration is invalid.

| Action | Contract |
| --- | --- |
| OPEN | Nonzero client nonce in `epoch`, other fields zero; creates a random server epoch after old hardware is released. Same nonce does not restart the session. |
| STATUS | Observe state/result, including a changed epoch after reset; does not restart any operation. |
| BEGIN | New, monotonically increasing operation ID, total symbols in `count`, carrier in `value`; allocates upload storage. |
| UPLOAD | Same epoch/ID, offset/count plus explicit symbols; sequential chunks, identical duplicates accepted, changed duplicates/gaps rejected. |
| COMMIT | Starts TX only after the complete frame passes validation. Repeated commit for that ID never transmits again, including after completion/cancel. |
| CAPTURE | New operation ID, timeout in `value`; starts a one-shot capture on the worker. |
| FETCH | Read an immutable completed capture by ID, offset and count. |
| CANCEL | Stops the current operation; its ID remains a tombstone. |
| CLOSE | Cancels, releases RMT/encoder/buffers and then reports CLOSED. |
| TORCH | New operation ID; bounded DC emission, no automatic renewal. |

BEGIN/CAPTURE/TORCH reject old IDs. The P4 never resends a COMMIT with an unknown
outcome: it polls STATUS. A C5 restart or changed SPI master session invalidates
the epoch and cancels old work. Idle sessions and incomplete uploads expire
after 2 seconds without a valid owning request. Foreign STATUS requests do not
renew the owner's lease. OPEN cannot take over until cleanup succeeds.

The C5 SPI dispatcher never waits for optical TX/RX. A core-0 worker at
`SYS_PRIO_SERVICE_HI` polls every 10 ms and owns all RMT lifecycle calls. The
short mutex uses nonblocking admission; callers may receive BUSY while the
worker is configuring/tearing down hardware. Each direction uses one 48-symbol
C5 RMT block without DMA. TX queue depth is one. Upload memory is owned by C5,
not by the SPI receive buffer or P4 caller.

Completion is published only after checked disable/delete/encoder cleanup.
Failed cleanup retains handles and storage and is retried by the worker; no
new operation can reuse them. The P4 similarly retains its resource/lease
until CLOSED or a safely closed replacement epoch is observed.

## Bounds

- TX: up to 4096 symbols (8192 pulses), total duration at most 900000 us.
- RX: up to 512 symbols. The C5 allocates one extra sentinel symbol; a full
  buffer is reported as `ESP_ERR_INVALID_SIZE` (overflow), never a valid prefix.
- RX timeout: 1–60000 ms; 1.25 us glitch filter and 12 ms idle delimiter.
  Long protocols containing larger gaps may require multiple captures.
- C5 TX completion deadline: 1200 ms. P4 completion reconciliation: 1500 ms.
- Control RPC: 250 ms per exchange; session open/close have a 1500 ms retry bound.
- Torch: hard **1000 ms maximum**, followed by closure even if polling continues.
  A fresh off/on cycle is required. This is a software containment limit, not
  an electrically validated continuous-current rating. Validate the populated
  LEDs, resistors and transistor before using DC mode on hardware.

Deadlines are task-scheduled, not independent hardware watchdogs. A stalled CPU
cannot be made safe by a software timer; boot GPIO initialization and the gate
pull-down supply the reset/off defaults.

## Public API

`ir_rx_init()` and `ir_tx_init()` acquire the resource and negotiate the remote
session. The initiating task owns it until release; a different task cannot
send, receive or tear it down. RX, TX and torch are mutually exclusive. App and
script calls also retain the existing `ir_lease`/persistent-worker permission
boundary. UI sends release after each operation. UI capture and companion RX
workers keep running until teardown is confirmed rather than exiting with an
orphaned owner.

`ir_rx_prime()` is retained for compatibility. Capture is armed by each
`ir_receive()` call, so ambient frames between calls do not consume the next
capture. `ir_receive()` returns decoded success, `ESP_ERR_NOT_FOUND` for an
undecoded complete frame, timeout, explicit overflow, or a transport/backend
error. Raw cache is cleared at capture start and only published after all
fragments validate.

`ir_get_last_raw()` reads that P4 cache without opening hardware. A destination
smaller than the complete frame returns `ESP_ERR_INVALID_SIZE`; it never silently
truncates. Cache remains available after closing the capture session.

`ir_send()` and `ir_send_raw()` upload a full frame, commit once and wait for
confirmed completion. Codecs, AC and file adapters call the same backend.
`ir_close()` returns a checked release result. Legacy `ir_rx_deinit()` and
`ir_tx_deinit()` remain void wrappers; app workers detect retained ownership
through `ir_lease_release()` and retry cleanup before another operation.
`ir_flash_on()` waits for confirmed activation; `ir_flash_off()` closes the
owning torch session. Diagnostic printing remains local.

## Build and validation

C5 `Service`, `esp_driver_rmt` and `esp_hal_rmt` compile with `-Os` to accommodate
this backend in the existing 2 MiB OTA slots. The other hardware Drivers and
C5 `sdkconfig.defaults` memory policy are unchanged. No generated dependency or
SDK source is patched. Heap buffers are allocated only for active uploads or
captures; the worker uses a fixed 4096-byte stack.

`python3 tools/ir_remote/test_remote.py` links the production P4 client, C5
backend, lease and codecs against a fake SPI/RMT HAL under ASan/UBSan. It covers
lost commit responses, duplicate commits, capture/replay, full-size raw upload,
AC signals, contention, invalid/missing/changed fragments, timeout, overflow,
failed teardown, session loss, torch expiry and incomplete-upload cleanup.
`tools/ir_api/test_ir_api.py` separately covers the production app/host-worker
adapters and permission checks against a fault-injected IR service.

Physical acceptance remains pending: optical replay/capture, carrier and mark
polarity on a logic analyzer, long AC frames, cancellation/reset while active,
DC current/temperature and simultaneous Wi-Fi/BLE/SPI load. Host tests cannot
establish RMT interrupt latency, optical range or the electrical torch limit.

## Decoded Frame (`ir_data_t`)

```c
typedef struct {
  ir_protocol_t protocol;
  uint32_t      address;
  uint32_t      command;
  bool          repeat;
} ir_data_t;
```

## Protocol Library (`ir_protocol.h`)

The library decodes and encodes consumer-remote protocols. `ir_decode` tries all known protocols against a symbol buffer; `ir_encode` produces symbols for a given `ir_data_t`. Pulse matching uses `IR_TOLERANCE` (25%) with a strict `IR_TOLERANCE_STRICT` (6%) for protocols whose preambles are close (Pioneer vs NEC).

| Protocol (`ir_protocol_t`) | Source | Carrier |
|----------------------------|--------|---------|
| `IR_PROTO_NEC` | `ir_protocol_nec.c` | 38 kHz |
| `IR_PROTO_NEC42` | `ir_protocol_nec42.c` | 38 kHz |
| `IR_PROTO_SAMSUNG` | `ir_protocol_samsung.c` | 38 kHz |
| `IR_PROTO_SONY` | `ir_protocol_sony.c` | 40 kHz |
| `IR_PROTO_RC5` | `ir_protocol_rc5.c` | 36 kHz |
| `IR_PROTO_RC6` | `ir_protocol_rc6.c` | 36 kHz |
| `IR_PROTO_RCA` | `ir_protocol_rca.c` | 38 kHz |
| `IR_PROTO_JVC` | `ir_protocol_jvc.c` | 38 kHz |
| `IR_PROTO_LG` | `ir_protocol_lg.c` | 38 kHz |
| `IR_PROTO_DENON` | `ir_protocol_denon.c` | 38 kHz |
| `IR_PROTO_PANASONIC` | `ir_protocol_panasonic.c` | 37 kHz |
| `IR_PROTO_PIONEER` | `ir_protocol_pioneer.c` | 40 kHz |

Carrier constants: `IR_CARRIER_HZ_DEFAULT` (38 kHz - NEC, Samsung, LG, JVC, Denon and others), `IR_CARRIER_HZ_RC5_RC6` (36 kHz), `IR_CARRIER_HZ_SONY` (40 kHz), `IR_CARRIER_HZ_PANASONIC` (37 kHz), `IR_CARRIER_HZ_PIONEER` (40 kHz). Use `ir_carrier_freq(proto)` to resolve a protocol's carrier and `ir_protocol_name(proto)` for its display name.

### Low-level codec building blocks

The pulse-distance and pulse-width primitives that the per-protocol files build on are also public:

```c
uint64_t ir_decode_pulse_distance(const rmt_symbol_word_t *symbols, size_t offset,
                                  size_t num_bits, const ir_pulse_distance_cfg_t *cfg);
uint64_t ir_decode_pulse_width(const rmt_symbol_word_t *symbols, size_t offset,
                               size_t num_bits, const ir_pulse_width_cfg_t *cfg);
size_t   ir_encode_pulse_distance(rmt_symbol_word_t *symbols, uint64_t data,
                                  size_t num_bits, const ir_encode_distance_cfg_t *cfg);
size_t   ir_encode_pulse_width(rmt_symbol_word_t *symbols, uint64_t data,
                               size_t num_bits, const ir_encode_width_cfg_t *cfg);
bool     ir_match(uint32_t measured_us, uint32_t expected_us);
bool     ir_match_tol(uint32_t measured_us, uint32_t expected_us, uint32_t tol_percent);
```

## Air-Conditioner Library (`ir_ac.h`)

Unlike remote protocols, each AC frame carries the full appliance state (power, mode, temperature, fan) rather than a single command. The full state is captured in `ir_ac_state_t`:

```c
typedef struct {
  ir_ac_protocol_t protocol;
  bool             power;
  ir_ac_mode_t     mode;   // AUTO, COOL, DRY, HEAT, FAN
  uint8_t          temp_c; // clamped to the target protocol's range at encode time
  ir_ac_fan_t      fan;    // AUTO, LOW, MED, HIGH
} ir_ac_state_t;
```

| AC protocol (`ir_ac_protocol_t`) | Source |
|----------------------------------|--------|
| `IR_AC_PROTO_COOLIX` | `ir_ac_coolix.c` |
| `IR_AC_PROTO_GREE` | `ir_ac_gree.c` |
| `IR_AC_PROTO_LG` | `ir_ac_lg.c` |
| `IR_AC_PROTO_MIDEA` | `ir_ac_midea.c` |
| `IR_AC_PROTO_TOSHIBA` | `ir_ac_toshiba.c` |
| `IR_AC_PROTO_HAIER` | `ir_ac_haier.c` |

```c
size_t      ir_ac_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);
esp_err_t   ir_ac_send(const ir_ac_state_t *state);   // requires ir_tx_init()
bool        ir_ac_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);
const char *ir_ac_protocol_name(ir_ac_protocol_t proto);
const char *ir_ac_mode_name(ir_ac_mode_t mode);
const char *ir_ac_fan_name(ir_ac_fan_t fan);
uint32_t    ir_ac_carrier_freq(ir_ac_protocol_t proto);
```

## Flipper IR File Format (`ir_file.h`)

Parses and serializes Flipper Zero `.ir` files. A file (`ir_file_t`) holds a grown-on-demand array of `ir_signal_t`, each either a decoded `ir_data_t` or a raw symbol/timing sequence.

```c
void        ir_file_init(ir_file_t *file);
void        ir_file_free(ir_file_t *file);
esp_err_t   ir_file_parse(const char *content, ir_file_t *file);          // append parsed signals
size_t      ir_file_to_string(const ir_file_t *file, char *buf, size_t buf_size);
ir_signal_t *ir_file_find(const ir_file_t *file, const char *name);
esp_err_t   ir_file_send(const ir_signal_t *signal);                       // dispatches ir_send_raw / ir_send
esp_err_t   ir_file_add_parsed(ir_file_t *file, const char *name, const ir_data_t *data);
esp_err_t   ir_file_add_raw(ir_file_t *file, const ir_file_add_raw_cfg_t *cfg);
```

Buffer ownership: `ir_file_parse` and `ir_file_add_raw` allocate the raw buffers, and `ir_file_free` releases everything. Limits: `IR_FILE_NAME_MAX` (32), `IR_FILE_PROTO_NAME_MAX` (32), `IR_FILE_LINE_BUF_SIZE` (1024), `IR_FILE_INITIAL_CAP` (8).

## Console Command

Registered by `register_ir_commands()` in `Service/console/commands/cmd_ir.c`. The `rx` and `send` subcommands are one-shot remote operations. Torch expires after 1000 ms even without `flash off`.

```
ir rx [timeout_ms]                        wait for one frame and decode it (default 5000 ms)
ir send <proto> <addr> <cmd> [repeat]     transmit a code (addr/cmd accept 0x hex)
ir flash <on|off>                         bounded DC torch (1000 ms maximum) / off
```

- `ir rx` calls `ir_rx_init` + `ir_rx_prime`, waits for a frame, prints protocol/address/command (and `[repeat]`), then `ir_rx_deinit`. Reports timeout and undecoded-frame cases distinctly.
- `ir send` resolves the protocol name (case-insensitive), parses `addr`/`cmd` (0x hex accepted), inits TX, sends, then deinits TX.
- `ir flash on` drives the emitter to full power (`ir_flash_on`); `ir flash off` turns it off. The output is infrared - check with a phone camera.

## Integrated IR library and validation

The IR menu opens a native browser with two sources: `/sdcard/ir/library` for
user remotes and `/assets/ir` for the bundled `tv.ir`, `audio.ir` and `ac.ir`
collections. If the SD library is missing or empty, it opens the built-in source.
A source selector at the root switches between them without copying or writing
files. The built-in source requires the runtime assets to be installed; an
app-only build does not restore them. It lists up
to 48 short names per directory, uses pages of 16 entries and bounds directory
depth to eight levels. Selecting an `.ir` file loads supported signals; selecting
a signal transmits it through `ir_store_send_signal()`.

`ir_file_parse()` reads full lines from a PSRAM buffer, limits input to 512 KiB
and RAW signals to 8192 pulses. Invalid fields reject the parse and free signals
appended by that call while preserving preexisting entries. Transmission rejects
RAW durations outside 1–32767 us, totals above 900000 us and carriers outside
10–100 kHz. Rejected timings are not truncated to fit RMT fields.

## App-worker lease

The [IR app ABI](../app_sdk/README.md#native-ir-api-abi-111) uses the persistent
host worker and the local-call force-kill gate. An internal task-owned lease is
acquired only while native channels, torch and in-flight native operations are
idle. While held, other tasks cannot initialize, receive, transmit, prime or
deinitialize the shared IR peripheral. Existing native UI/companion sessions are
refused rather than reused. Nested calls from the owning worker remain valid.

Decoded app RX is bounded to 1–1000 ms; decoded TX uses the existing RMT wait.
Both paths release channels and the lease before returning. If RMT disable,
channel deletion or encoder deletion fails, the driver retains the corresponding
handle and resource grant. The worker retains its lease, blocking conflicting
callers; a later local IR operation retries cleanup before starting new work.
Init failures use the same checked teardown. No new app-created hardware task
or firmware-pointer access is introduced.

`tools/ir_api/test_ir_api.py` compiles production lease, host worker and ABI
adapters against a fake IR service. Actual remote lifecycle and RMT retention
are exercised separately by `tools/ir_remote/test_remote.py`. Optical operation
remains part of physical acceptance.


## App and script adapters

ABI 1.16 exposes decoded RX/TX, signed raw capture/replay, single-signal SD-file
replay and AC state encoding through the shared app table and JS/Lua bindings.
Extended calls execute under the persistent host worker's IR lease; heap symbol
storage survives failed RMT teardown and is reclaimed only after cleanup succeeds.
This avoids freeing replay/AC symbols while a timed-out transmit may still use
them. See the [app contract](../app_sdk/README.md#complete-ir-adapters-abi-116)
and [script calls](../scripts/README.md#complete-ir-bindings-abi-116).
