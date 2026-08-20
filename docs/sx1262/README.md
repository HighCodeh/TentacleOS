# SX1262 LoRa Transceiver Driver

This component provides a driver for the Semtech SX1262 sub-GHz LoRa transceiver. It handles the full SX1262 command set over SPI, LoRa modulation/packet configuration, TX/RX (single, continuous, and duty-cycle), Channel Activity Detection (CAD), sleep/wake power management, and interrupt-driven packet reception with a ring buffer.

The driver core is platform-agnostic: all hardware access is delegated through a HAL callback struct (`sx1262_hal_t`), and the ESP32 port lives in a single file. This keeps the register/command logic portable and testable.

## Overview

- **Location:** `components/Drivers/sx1262/`
- **Public header:** `include/sx1262.h` (types in `include/sx1262_types.h`)
- **Dependencies:** `spi`, `pins` (`pin_def.h`), `sys_prio`, `driver/spi_master`, `driver/gpio`, `freertos`, `esp_log`
- **Interface:** SPI3_HOST at 4 MHz, SPI mode 0 (shared bus - see below)
- **Reference:** register/opcode comments cite the SX1262 datasheet (DS) sections

## File Layout

| File | Responsibility |
|------|----------------|
| `sx1262.c` | Public API, init/bring-up sequence, config, IRQ task, recovery |
| `sx1262_cmd.c` | Low-level SPI opcode/register/buffer access and BUSY-pin waiting |
| `sx1262_fsm.c` | Radio state machine (tracks `sx1262_state_t`) |
| `sx1262_hal.c` | ESP32 HAL port: SPI device, GPIO, mutex, bus lock, antenna switch |
| `sx1262_irq.c` | IRQ dispatch, RX packet read, RX ring buffer |
| `sx1262_radio.c` | TX/RX/CAD/sleep/wake/duty-cycle operations |
| `sx1262_regs.h` | Opcodes, register addresses, LoRa parameter constants |

## Shared SPI3 Bus Contract (important)

The SX1262 sits on **SPI3_HOST**, the same bus as the **ST7789 display**. Because two independent drivers share one bus, every SX1262 transaction is serialized through a layered lock in the HAL (`hal_lock` / `hal_unlock` in `sx1262_hal.c`):

1. Take the SX1262 device mutex (`spi_mutex`, `portMAX_DELAY`).
2. Take the shared SPI3 bus lock via `spi_bus_lock_take(SPI3_BUS_LOCK_TIMEOUT_MS)` (1000 ms) - this is the cross-component handshake with the display driver.
3. `spi_device_acquire_bus()` for the duration of the transaction.

Unlock reverses the order (`release_bus` -> `spi_bus_give` -> give mutex). The bus lock is taken/released per transaction so the display is never starved.

Bus lifecycle:

- `sx1262_hal_create()` calls `spi_bus_initialize(SPI3_HOST, ...)`. If the bus is **already initialized** by another driver (the ST7789 display, via the kernel `spi_init`), the returned `ESP_ERR_INVALID_STATE` is treated as success - the SX1262 simply adds itself as a second device.
- `sx1262_hal_destroy()` removes only the SX1262 device and deletes the device mutex. **It never frees the SPI3 bus**, because the display still needs it. `sx1262_deinit()` -> `sx1262_hal_destroy()` therefore leaves the display fully functional.

## RX Robustness / Hardening

The RX path is hardened against errored packets and a flaky/contended SPI3 bus:

- **Retry bring-up until STDBY_RC.** `sx1262_init()` runs `sx1262_hw_bringup()` up to `SX1262_INIT_MAX_ATTEMPTS` (3) times. Bring-up performs reset -> standby -> DCDC regulator -> full calibration -> image calibration -> workarounds -> LoRa config, then reads the chip status and **only succeeds if the chip actually landed in STDBY_RC** (`chip_mode == STDBY_RC`). Any device error reported after calibration also fails the attempt, triggering a retry.
- **IRQ-task self-recovery.** The IRQ task (`irq_task`) counts consecutive `sx1262_irq_process()` failures; after `SX1262_IRQ_FAIL_RECOVER` (5) in a row it calls `sx1262_recover()`, which does a hardware reset + full reconfigure (`sx1262_hw_bringup`) and resumes continuous RX. A single success resets the streak.
- **Errored packets do not read the FIFO.** In `read_rx_packet()`, if the RxDone IRQ arrives with CRC-error or header-error flags set, the payload buffer is **not** read (`len = 0`), avoiding acting on corrupt FIFO contents. RSSI/SNR and the `has_crc_error` / `has_header_error` flags are still populated so the caller can observe the failure. Standalone CRC/header errors (no RxDone) are surfaced through the `on_error` callback.
- **Demoted RX-error hot-path logs.** RxDone, CRC error, header error, timeout, CAD, and ring-buffer-full messages log at `ESP_LOGD` (debug), not error/warning. This keeps the hot path quiet under noisy conditions instead of flooding the console on every errored packet.
- **Bounded RX ring buffer.** Received packets are pushed into a fixed ring of `SX1262_RX_RING_SIZE` (8) entries; a full ring drops the newest packet (debug log). Ring access is guarded by the HAL critical section, so `sx1262_get_packet()` is safe against the IRQ task.

## Radio Parameters (`sx1262_config_t`)

```c
typedef struct {
  sx1262_hal_t hal;       // Platform HAL - all callbacks
  uint32_t frequency_hz;  // 150_000_000 .. 960_000_000 Hz
  uint8_t sf;             // SF5 .. SF12
  uint8_t bw;             // BW_7 .. BW_500
  uint8_t cr;             // CR_4_5 .. CR_4_8
  int8_t tx_power_dbm;    // -9 .. +22 dBm
  uint16_t preamble_len;  // preamble symbols (min 2, 12+ recommended)
  bool is_crc_on;
  bool is_inverted_iq;    // true = LoRaWAN downlink (activates workaround W4)
  bool is_implicit_hdr;   // true = implicit header (activates W3); SF6 requires this
  bool is_public_network; // sync word 0x3444 (public) vs 0x1424 (private/Meshtastic)
} sx1262_config_t;
```

Validation (`validate_config`) rejects out-of-range values before any SPI transaction. Notable rules: `BW_500` requires `SF >= 6`, and `SF6` requires implicit-header mode. LDRO (low data rate optimize) is derived automatically for `SF11/SF12` at `BW <= 125 kHz`.

## Pin / Hardware Configuration

Pins come from `pin_def.h` (`components/Drivers/pins`):

| Signal | Macro | GPIO |
|--------|-------|------|
| SCLK   | `GPIO_LORA_SCLK_PIN`  | 21 |
| MOSI   | `GPIO_LORA_MOSI_PIN`  | 22 |
| MISO   | `GPIO_LORA_MISO_PIN`  | 23 |
| NSS/CS | `GPIO_LORA_CS_PIN`    | 26 |
| BUSY   | `GPIO_LORA_BUSY_PIN`  | 4  |
| DIO1   | `GPIO_LORA_DIO1_PIN`  | 5  |
| NRESET | `GPIO_LORA_RESET_PIN` | -1 (not wired) |
| TXEN   | `GPIO_LORA_TXEN_PIN`  | -1 (not wired) |
| RXEN   | `GPIO_LORA_RXEN_PIN`  | -1 (not wired) |

`-1` pins are skipped by the HAL. With no discrete TX/RX antenna-switch pins, the RF switch is driven by the chip itself via **DIO2 as RF switch** (`SetDIO2AsRfSwitch`, enabled during bring-up). NSS is toggled in software (the SPI device is configured with `spics_io_num = -1`). BUSY and DIO1 are inputs; DIO1 signals IRQs. Reset/wait timings: `SX1262_RESET_HOLD_MS` (2), `SX1262_RESET_WAIT_MS` (20); BUSY polling caps at `SX1262_WAIT_BUSY_TIMEOUT_MS` (100).

## HAL Contract (`sx1262_hal_t`)

The driver core never includes platform headers. Porting means implementing the callback struct: `spi_transfer`, `cs_low` / `cs_high`, `reset_write`, `busy_read`, `delay_ms`, `get_tick_ms`, `lock` / `unlock`, `enter_critical` / `exit_critical`, `set_antenna`, and an opaque `ctx`. `sx1262_hal_create()` populates it for the ESP32; a different platform supplies its own file.

## API Reference

### Lifecycle

```c
esp_err_t sx1262_init(const sx1262_config_t *config);
esp_err_t sx1262_deinit(void);
esp_err_t sx1262_start(void);
void      sx1262_stop(void);
esp_err_t sx1262_config_lora(const sx1262_config_t *config);
esp_err_t sx1262_set_callbacks(const sx1262_callbacks_t *cbs);
bool      sx1262_is_running(void);
```

- `sx1262_init` validates HAL + config, then runs the retrying bring-up (see hardening). Returns `ESP_ERR_INVALID_ARG` on bad config/HAL, or the last bring-up error after all attempts fail.
- `sx1262_start` creates the DIO1 IRQ processing task (stack 4096, `SYS_PRIO_REALTIME`, `SYS_CORE_RADIO` = core 0). `ESP_ERR_INVALID_STATE` if already running or not initialized; `ESP_ERR_NO_MEM` on task-create failure.
- `sx1262_stop` signals the IRQ task and waits (up to 500 ms) for it to exit via task notification, then turns the antenna switch off.
- `sx1262_config_lora` reconfigures LoRa parameters at runtime; re-applies workaround W4 (IQ polarity) every call. The stored HAL is preserved across the config copy.

### Callbacks (`sx1262_callbacks_t`)

```c
esp_err_t sx1262_set_callbacks(const sx1262_callbacks_t *cbs);
```

Registers `on_tx_done`, `on_rx_done`, `on_cad_done`, `on_timeout`, `on_error` (each may be NULL) plus a shared `cb_ctx`. Callbacks fire from the IRQ task, not an ISR.

### TX / RX

```c
esp_err_t sx1262_transmit(const uint8_t *data, uint8_t len, uint32_t timeout_ms);
esp_err_t sx1262_receive_single(uint32_t timeout_ms);
esp_err_t sx1262_receive_continuous(void);
void      sx1262_stop_rx(void);
esp_err_t sx1262_get_packet(sx1262_packet_t *out_packet);
```

- `sx1262_transmit` is non-blocking (payload 1..255 bytes); completion arrives via `on_tx_done`. Applies workaround W1 (BW500 sensitivity) before each TX.
- `sx1262_receive_single` returns to STDBY after one packet or timeout (`timeout_ms = 0` = wait forever); `sx1262_receive_continuous` stays in RX until `sx1262_stop_rx()`.
- `sx1262_get_packet` dequeues from the RX ring; `ESP_ERR_NOT_FOUND` when empty.

Received packet (`sx1262_packet_t`) carries `buf[256]`, `len`, `rssi_pkt_dbm`, `snr_pkt_db`, `signal_rssi_dbm`, `has_crc_error`, and `has_header_error`.

### CAD & Power

```c
esp_err_t sx1262_cad_start(void);
esp_err_t sx1262_sleep(bool is_warm);
esp_err_t sx1262_wakeup(void);
esp_err_t sx1262_set_rx_duty_cycle(uint32_t rx_ms, uint32_t sleep_ms);
```

- `sx1262_sleep(false)` (cold start) marks the driver as needing re-init before the next TX/RX; `true` is a warm start that retains config.
- `sx1262_set_rx_duty_cycle` runs wake-on-radio (RX window then sleep window, repeating in hardware).

### Status / Diagnostics

```c
sx1262_state_t sx1262_get_state(void);
esp_err_t sx1262_get_status(uint8_t *out_status);
esp_err_t sx1262_get_device_errors(uint16_t *out_errors);
esp_err_t sx1262_get_rssi_inst(int16_t *out_rssi_dbm);
esp_err_t sx1262_get_stats(sx1262_stats_t *out_stats);
esp_err_t sx1262_process_irq(void);
```

`sx1262_get_stats` returns cumulative counters (`nb_pkt_received`, `nb_crc_error`, `nb_header_error`). `sx1262_process_irq` is called by the IRQ task when DIO1 rises; it is not for direct ISR use.

## Chip Workarounds

The driver applies the datasheet errata workarounds automatically: **W1** (BW500 sensitivity, before each TX), **W2** (TX clamp config, during bring-up), **W3** (implicit-header RX, when `is_implicit_hdr`), and **W4** (IQ polarity, on every `config_lora`).
