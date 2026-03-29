# SubGhz Application

This component implements the complete Sub-GHz RF application layer: signal reception (with protocol decoding and frequency hopping), raw/encoded transmission, spectrum analysis, signal analysis, and file serialization. It sits on top of the `cc1101` driver and uses the ESP-IDF RMT peripheral for precise pulse timing.

## Overview

- **Location:** `components/Applications/SubGhz/`
- **Dependencies:** `cc1101`, `driver/rmt_rx`, `driver/rmt_tx`, `freertos`, `pin_def`
- **RMT Resolution:** 1 MHz (1 us per tick)
- **RX GPIO:** GPIO 8 (GDO0 via `GPIO_SDA_PIN`)
- **TX GPIO:** GDO2 (via `GPIO_SCL_PIN`)

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

Captures RF signals via the CC1101 GDO0 pin routed to the ESP32 RMT RX peripheral. Runs as a FreeRTOS task pinned to Core 1.

**Operating Modes:**

| Mode | Behavior |
|------|----------|
| `SUBGHZ_MODE_SCAN` | Decodes signals via protocol registry. Unknown signals are analyzed and saved as RAW. |
| `SUBGHZ_MODE_RAW` | Captures and saves all raw pulse data without decoding. |

**Frequency Hopping:** When `freq == 0` is passed to `subghz_receiver_start`, the receiver cycles through 12 predefined frequencies (433.92, 868.35, 315, 300, 390, 418, 915 MHz, etc.) every 5 seconds.

**Signal Processing Pipeline:**
1. RMT hardware captures pulse timings (min 1 us, idle timeout 10 ms)
2. Software filter removes pulses < 15 us
3. Pulses converted to signed int32 buffer (positive = HIGH, negative = LOW)
4. **SCAN mode:** Protocol registry tries all decoders -> Analyzer for unknowns
5. **RAW mode:** Direct save to storage

#### API

```c
void subghz_receiver_start(subghz_mode_t mode, cc1101_preset_t preset, uint32_t freq);
void subghz_receiver_stop(void);
bool subghz_receiver_is_running(void);
```
- `freq = 0` enables frequency hopping mode.
- Task stack: 8192 bytes, priority 5, Core 1.

---

### Transmitter (`subghz_transmitter`)

Asynchronous queue-based transmitter. Converts signed pulse timings to RMT symbols and transmits via CC1101 GDO2 in async mode.

**Flow:** `subghz_tx_send_raw()` -> FreeRTOS Queue -> TX Task -> RMT TX -> CC1101

#### API

```c
void subghz_tx_init(void);
void subghz_tx_stop(void);
void subghz_tx_send_raw(const int32_t *timings, size_t count);
```
- Queue depth: 10 items. Drops packets if full.
- Timing data is copied internally; caller retains ownership of the original buffer.
- Max RMT symbol duration: 32767 us per pulse.
- Task stack: 4096 bytes, priority 5, Core 1.

---

### Spectrum Analyzer (`subghz_spectrum`)

Sweeps across a frequency span by stepping the CC1101 through discrete frequencies and reading RSSI values. Produces 80-sample spectral lines.

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
    float dbm_values[80];   // RSSI in dBm per bin
    uint64_t timestamp;
} subghz_spectrum_line_t;
```

#### API

```c
void subghz_spectrum_start(uint32_t center_freq, uint32_t span_hz);
void subghz_spectrum_stop(void);
bool subghz_spectrum_get_line(subghz_spectrum_line_t* out_line);
```
- Task stack: 4096 bytes, priority 1, Core 1.
- Thread-safe reads via `subghz_spectrum_get_line`.

---

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
    uint32_t estimated_te;       // Base time element (us)
    uint32_t pulse_min;
    uint32_t pulse_max;
    size_t pulse_count;
    const char* modulation_hint; // "PWM", "Manchester" or "Unknown"
    uint8_t bitstream[128];      // Recovered bits
    size_t bitstream_len;
} subghz_analyzer_result_t;
```

#### API

```c
bool subghz_analyzer_process(const int32_t* pulses, size_t count, subghz_analyzer_result_t* out_result);
```
- Requires minimum 10 pulses. Filters durations < 50 us as noise.

---

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

#### API

```c
uint8_t subghz_protocol_get_preset_id(void);
size_t subghz_protocol_serialize_decoded(const subghz_data_t* data, uint32_t frequency, uint32_t te, char* out_buf, size_t out_size);
size_t subghz_protocol_serialize_raw(const int32_t* pulses, size_t count, uint32_t frequency, char* out_buf, size_t out_size);
size_t subghz_protocol_parse_raw(const char* content, int32_t* out_pulses, size_t max_count, uint32_t* out_frequency, uint8_t* out_preset);
```

---

### Storage (`subghz_storage`)

Saves captured signals to persistent storage using the serializer. Currently operates in placeholder mode (outputs to stdout).

#### API

```c
void subghz_storage_init(void);
void subghz_storage_save_decoded(const char* name, const subghz_data_t* data, uint32_t frequency, uint32_t te);
void subghz_storage_save_raw(const char* name, const int32_t* pulses, size_t count, uint32_t frequency);
```

---

## Protocol Plugins (`protocols/`)

The protocol system follows a **plugin architecture**. Each protocol is a self-contained module (e.g., `protocol_princeton.c`) that implements a common interface and is registered in a central registry. This design allows adding support for new protocols without modifying existing code — just create a new `protocol_*.c` file, implement the `subghz_protocol_t` interface, and register it in `subghz_protocol_registry.c`.

### Plugin Interface

Every protocol plugin must export a `subghz_protocol_t` struct with two function pointers:

```c
typedef struct {
    const char* name;
    bool (*decode)(const int32_t* pulses, size_t count, subghz_data_t* out_data);
    size_t (*encode)(const subghz_data_t* data, int32_t* pulses, size_t max_count);
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
   - Add `&protocol_mydevice` to the `protocols[]` array

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
| Rossi        | OOK/PWM    | Gate remotes                 |

### Utility Functions (`subghz_protocol_utils.h`)

```c
uint32_t subghz_abs_diff(uint32_t a, uint32_t b);
bool subghz_check_pulse(int32_t raw_len, uint32_t target_len, uint8_t tolerance_pct);
```
Helper functions available to all plugins for pulse timing validation with percentage-based tolerance.

### Registry API

```c
void subghz_protocol_registry_init(void);
bool subghz_protocol_registry_decode_all(const int32_t* pulses, size_t count, subghz_data_t* out_data);
const subghz_protocol_t* subghz_protocol_registry_get_by_name(const char* name);
```

## Common Types (`subghz_types.h`)

```c
typedef struct {
    const char* protocol_name;
    uint32_t serial;
    uint8_t btn;
    uint8_t bit_count;
    uint32_t raw_value;
} subghz_data_t;
```

Shared data structure used across decoder, serializer, storage, and UI layers.
