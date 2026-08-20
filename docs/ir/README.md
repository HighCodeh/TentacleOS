# IR TX/RX Service

This component provides the infrared transmit/receive service for TentacleOS on the ESP32-P4. It drives the IR emitter and detector through the RMT peripheral, ships a full protocol decode/encode library (consumer remotes and air-conditioner state frames), a Flipper-Zero-compatible file format, a console command, and a full-power torch (flashlight) mode.

## Overview

- **Location:** `firmware_p4/components/Service/ir/`
- **Target:** `firmware_p4` only (ESP32-P4)
- **Main header:** `include/ir.h`
- **Companion headers:** `include/ir_protocol.h` (remote protocols), `include/ir_ac.h` (air-conditioner protocols), `include/ir_file.h` (Flipper IR file format)
- **Dependencies:** `pin_def`, `led_control`, `driver/rmt_tx`, `driver/rmt_rx`, `driver/rmt_encoder`, `driver/gpio`, `freertos`
- **Peripheral:** RMT (one RX channel + one TX channel), non-DMA
- **Console command:** `ir` (registered in `Service/console/commands/cmd_ir.c`)

The service is brought up lazily - there is no boot-time init. Callers init the RX or TX channel on demand and release it when done, so RMT channels are not held for the whole session.

## Hardware / Bus

| Item | Value | Source |
|------|-------|--------|
| TX pin | `GPIO_IR_TX_PIN` = GPIO 0 | `pin_def.h` |
| RX pin | `GPIO_IR_RX_PIN` = GPIO 1 | `pin_def.h` |
| RMT resolution | 1 MHz (1 tick = 1 us) | `IR_RMT_RESOLUTION_HZ` |
| TX carrier duty | 0.33 | `IR_CARRIER_DUTY_CYCLE` |
| Default carrier | 38 kHz (protocol-dependent) | `ir_carrier_freq()` |

The emitter is infrared and invisible to the eye: verify output with a phone camera. The TX pin rests low via an external pull-down (R90).

### RMT tuning constants (`ir.h`)

| Constant | Value | Meaning |
|----------|-------|---------|
| `IR_RMT_MEM_SYMBOLS` | 128 | Encode buffer size for a single frame |
| `IR_MAX_SYMBOLS` | 512 | User RX/last-frame buffer depth |
| `IR_RX_MEM_BLOCK_SYMBOLS` | 96 | Non-DMA RX FIFO depth (2x the P4 48-word block) |
| `IR_TX_MEM_BLOCK_SYMBOLS` | 64 | TX channel FIFO (2 P4 channels) |
| `IR_RX_MIN_NS` / `IR_RX_MAX_NS` | 1250 / 12000000 | RX glitch/timeout window |
| `IR_TX_QUEUE_DEPTH` | 4 | TX transaction queue depth |
| `IR_TX_WAIT_MS` | 1000 | Blocking wait for a transmit to finish |
| `IR_PRINT_MAX_SYMBOLS` | 40 | Cap on symbols logged by `ir_print_raw` |

RX and TX are non-DMA on this board: DMA-backed `rmt_receive()` failed here, so the RX channel uses a ping-ponged FIFO into the `IR_MAX_SYMBOLS` user buffer.

## API Reference

### Channel lifecycle

#### `ir_rx_init` / `ir_rx_deinit`
```c
esp_err_t ir_rx_init(void);
void      ir_rx_deinit(void);
```
`ir_rx_init` creates the RMT RX channel on `GPIO_IR_RX_PIN`, its event queue and callback, enables it, and arms a one-shot receive. Returns `ESP_OK` (idempotent if already inited), `ESP_ERR_NO_MEM` on mutex/queue failure, or an RMT error. `ir_rx_deinit` disables and deletes the channel and queue (no-op if RX was never inited).

#### `ir_rx_prime`
```c
void ir_rx_prime(void);
```
Resets the RX queue (drops a stale/ambient frame) and re-arms the one-shot receive if a prior frame left it idle. Call at the start of every capture: a one-shot `rmt_receive()` is consumed by every frame, including NEC repeat frames, so without priming the next `ir_receive()` could wait forever.

#### `ir_tx_init` / `ir_tx_deinit`
```c
esp_err_t ir_tx_init(void);
void      ir_tx_deinit(void);
```
`ir_tx_init` creates the RMT TX channel on `GPIO_IR_TX_PIN`, a copy encoder, and enables the channel. Returns `ESP_OK` (idempotent) or an RMT error. `ir_tx_deinit` disables and deletes the channel and encoder and resets the cached carrier (no-op if TX was never inited).

### Receive / decode

#### `ir_receive`
```c
esp_err_t ir_receive(ir_data_t *out_data, uint32_t timeout_ms);
```
Waits up to `timeout_ms` for a frame, snapshots the raw symbols (for `ir_get_last_raw`), decodes with `ir_decode`, and re-arms the channel. Signals the RGB LED (info on decode, warning on timeout/undecoded). Returns `ESP_OK` on decode, `ESP_ERR_TIMEOUT` if nothing arrived, `ESP_ERR_NOT_FOUND` if a frame arrived but did not decode, or an RMT error.

#### `ir_get_last_raw`
```c
esp_err_t ir_get_last_raw(rmt_symbol_word_t *out_buf, size_t buf_max, size_t *out_count);
```
Copies the raw RMT symbols from the most recent received frame (mutex-guarded). `out_count` may be NULL. Returns `ESP_ERR_INVALID_ARG` on a NULL/zero buffer, `ESP_ERR_TIMEOUT` if the mutex could not be taken.

### Transmit / encode

#### `ir_send`
```c
esp_err_t ir_send(const ir_data_t *data);
```
Encodes `data` with `ir_encode` and transmits it at the protocol's carrier frequency. Requires `ir_tx_init`. Returns `ESP_ERR_INVALID_ARG` if encoding produced no symbols, else the transmit result.

#### `ir_send_raw`
```c
esp_err_t ir_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz);
```
Transmits a raw symbol array. Pass `carrier_hz = 0` to disable the carrier. Returns `ESP_ERR_INVALID_ARG` on a NULL/empty array.

### Torch / flashlight

#### `ir_flash_on` / `ir_flash_off`
```c
esp_err_t ir_flash_on(void);
esp_err_t ir_flash_off(void);
```
`ir_flash_on` drives the emitter fully on as DC with no carrier: it releases the RMT TX channel (`ir_tx_deinit`) so it does not fight for the pin, reconfigures `GPIO_IR_TX_PIN` as a plain output, and holds it high at full power. This is continuous full-power drive - use it for tests or short bursts, do not leave it on long. `ir_flash_off` drives the pin low again (a later `ir_tx_init` re-routes it back to RMT). Both return `ESP_OK` or a GPIO driver error.

### Logging helpers

```c
void ir_print_raw(const rmt_symbol_word_t *symbols, size_t count); // DEBUG level, capped at IR_PRINT_MAX_SYMBOLS
void ir_print_data(const ir_data_t *data);                         // INFO level: protocol, address, command, repeat
```

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

Registered by `register_ir_commands()` in `Service/console/commands/cmd_ir.c`. The `rx` and `send` subcommands are one-shot: they init the RMT channel on demand and release it when done. `flash` is the exception - it stays on until `flash off`.

```
ir rx [timeout_ms]                        wait for one frame and decode it (default 5000 ms)
ir send <proto> <addr> <cmd> [repeat]     transmit a code (addr/cmd accept 0x hex)
ir flash <on|off>                         drive the emitter at full power (torch) / off
```

- `ir rx` calls `ir_rx_init` + `ir_rx_prime`, waits for a frame, prints protocol/address/command (and `[repeat]`), then `ir_rx_deinit`. Reports timeout and undecoded-frame cases distinctly.
- `ir send` resolves the protocol name (case-insensitive), parses `addr`/`cmd` (0x hex accepted), inits TX, sends, then deinits TX.
- `ir flash on` drives the emitter to full power (`ir_flash_on`); `ir flash off` turns it off. The output is infrared - check with a phone camera.
