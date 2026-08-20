# YS-RFID2 UART RFID Reader Driver

This component drives the YS-RFID2 serial RFID reader module. It reads the module's ASCII output over UART, parses the "card number" frames into card IDs, and delivers card-detected / card-removed events to the application through a callback. It is `firmware_p4` only.

## Overview

- **Location:** `firmware_p4/components/Drivers/ys_rfid2/`
- **Header:** `include/ys_rfid2.h`
- **Dependencies:** `pin_def`, `sys_prio`, `driver/uart`, `esp_timer`, `freertos`
- **Interface:** UART (via the ESP-IDF `driver/uart` layer, wrapped by the local HAL)
- **UART port:** `UART_NUM_2` (default)
- **Baud rate:** 9600 8N1, no flow control (default)
- **Pins:** TX = GPIO 24 (`GPIO_RFID_UART_TX_PIN`), RX = GPIO 25 (`GPIO_RFID_UART_RX_PIN`)

## Module Structure

The driver is split into three layers:

| Layer  | Files | Responsibility |
|--------|-------|----------------|
| Core   | `ys_rfid2_core.c`, `include/ys_rfid2.h`, `include/ys_rfid2_types.h` | Public API, state machine, background reader task, debounce and removal logic, event dispatch. |
| Parser | `ys_rfid2_parser.c`, `include/ys_rfid2_parser.h` | Byte-by-byte frame accumulation, card ID extraction and validation, decimal-to-bytes conversion. |
| HAL    | `hal/ys_rfid2_hal_uart.c`, `hal/include/ys_rfid2_hal_uart.h` | Thin UART wrapper: install/config/pins, read, write, flush. |

Data flow: HAL UART reads raw bytes -> core reader task feeds each byte to the parser -> a completed valid frame becomes a `YS_RFID2_EVENT_CARD_DETECTED` event delivered to the user callback.

## Frame Format and Parsing

The module emits ASCII lines of the form:

```
card number: xxxxxxxxxx@
```

The parser (`ys_rfid2_parser_feed`) accumulates bytes into a 64-byte line buffer until it sees the `@` delimiter. On the delimiter it searches the buffer for the `"card number: "` prefix (13 chars) followed by exactly 10 decimal digits (`YS_RFID2_CARD_ID_LEN`). If found and all 10 characters are digits `0-9`, it:

- Copies the 10-digit ASCII string into `id_str` (NUL-terminated).
- Converts the decimal string to 5 big-endian raw bytes (40 bits) in `data`.
- Sets `bit_count` to 40.

Any byte that overflows the line buffer resets the accumulator. The parser holds a single static line buffer, so it is not reentrant.

## Data Types (`ys_rfid2_types.h`)

Constants:

| Constant | Value | Meaning |
|----------|-------|---------|
| `YS_RFID2_CARD_ID_LEN` | 10 | Card ID string length (decimal digits) |
| `YS_RFID2_RAW_DATA_LEN` | 5 | Raw byte count (40 bits) |

### `ys_rfid2_state_t`

`YS_RFID2_STATE_UNINITIALIZED`, `YS_RFID2_STATE_IDLE`, `YS_RFID2_STATE_SCANNING`, `YS_RFID2_STATE_ERROR`, `YS_RFID2_STATE_COUNT`.

### `ys_rfid2_event_type_t`

`YS_RFID2_EVENT_CARD_DETECTED`, `YS_RFID2_EVENT_CARD_REMOVED`, `YS_RFID2_EVENT_COUNT`.

### `ys_rfid2_raw_data_t`
```c
typedef struct {
  char id_str[YS_RFID2_CARD_ID_LEN + 1];  // 10-digit ID + NUL
  uint8_t data[YS_RFID2_RAW_DATA_LEN];    // 5 raw bytes (40 bits, big-endian)
  uint8_t bit_count;                      // 40 for a parsed card
} ys_rfid2_raw_data_t;
```

### `ys_rfid2_event_t`
```c
typedef struct {
  ys_rfid2_event_type_t type;
  ys_rfid2_raw_data_t raw;
  int64_t timestamp_ms;   // esp_timer time in ms at detection
} ys_rfid2_event_t;
```

### `ys_rfid2_event_cb_t`
```c
typedef void (*ys_rfid2_event_cb_t)(const ys_rfid2_event_t *event, void *ctx);
```
Invoked from the reader task context. Must not block for extended periods. The `event` pointer is valid only during the callback.

### `ys_rfid2_config_t`
```c
typedef struct {
  int uart_port;
  int baud_rate;
  int tx_pin;
  int rx_pin;
  uint32_t debounce_ms;
  uint32_t removal_timeout_ms;
} ys_rfid2_config_t;
```

## Configuration and Tunables

`ys_rfid2_default_config()` returns:

| Field | Default | Source |
|-------|---------|--------|
| `uart_port` | `UART_NUM_2` | fixed default |
| `baud_rate` | 9600 | `RFID_DEFAULT_BAUD` |
| `tx_pin` | GPIO 24 | `GPIO_RFID_UART_TX_PIN` (pin_def.h) |
| `rx_pin` | GPIO 25 | `GPIO_RFID_UART_RX_PIN` (pin_def.h) |
| `debounce_ms` | 1000 | `RFID_DEFAULT_DEBOUNCE_MS` |
| `removal_timeout_ms` | 2000 | `RFID_DEFAULT_REMOVAL_MS` |

- **debounce_ms:** while the same card ID keeps being read, repeat detections within this window are suppressed (the timer is refreshed on each read). A different card ID fires immediately.
- **removal_timeout_ms:** if no byte is read for longer than this after the last detection, a `YS_RFID2_EVENT_CARD_REMOVED` event fires for the last card.

Other internal constants (`ys_rfid2_core.c`): reader task stack 4096 bytes, priority `SYS_PRIO_SERVICE_HI`, UART read timeout 100 ms. The task is pinned to `SYS_CORE_RADIO` (core 0) via `xTaskCreatePinnedToCore`.

## API Reference

### Configuration

#### `ys_rfid2_default_config`
```c
ys_rfid2_config_t ys_rfid2_default_config(void);
```
Returns the default configuration described above.

### Lifecycle

#### `ys_rfid2_init`
```c
esp_err_t ys_rfid2_init(const ys_rfid2_config_t *config);
```
Creates the driver mutex and initializes the UART via the HAL. Does NOT start scanning. Pass `NULL` to use the default config. Returns `ESP_OK`, `ESP_ERR_INVALID_STATE` if already initialized, `ESP_ERR_NO_MEM` if mutex creation fails, or a propagated HAL error.

#### `ys_rfid2_deinit`
```c
esp_err_t ys_rfid2_deinit(void);
```
Stops scanning if active, tears down the UART, and deletes the mutex. Returns `ESP_OK`, or `ESP_ERR_INVALID_STATE` if not initialized.

### Scanning

#### `ys_rfid2_start`
```c
esp_err_t ys_rfid2_start(ys_rfid2_event_cb_t cb, void *ctx);
```
Resets the parser, flushes the UART input, and creates the background reader task that delivers events via `cb`. `cb` must not be NULL. Returns `ESP_OK`, `ESP_ERR_INVALID_ARG` if `cb` is NULL, `ESP_ERR_INVALID_STATE` if not initialized (state must be IDLE), or `ESP_ERR_NO_MEM` if task creation fails.

#### `ys_rfid2_stop`
```c
void ys_rfid2_stop(void);
```
Signals the reader task to stop and blocks until it has exited, then clears the callback.

### State and Query

#### `ys_rfid2_get_state`
```c
ys_rfid2_state_t ys_rfid2_get_state(void);
```
Returns the current driver state.

#### `ys_rfid2_get_last_card`
```c
esp_err_t ys_rfid2_get_last_card(ys_rfid2_event_t *out_event);
```
Copies the last detected card event into `out_event` (mutex-guarded, 100 ms take timeout). Returns `ESP_OK` if a card was previously detected, `ESP_ERR_NOT_FOUND` if none yet, `ESP_ERR_INVALID_ARG` if `out_event` is NULL, or `ESP_ERR_TIMEOUT` if the mutex could not be taken.

## HAL UART Layer (`ys_rfid2_hal_uart.h`)

A thin wrapper over ESP-IDF `driver/uart`. It installs the driver with a 256-byte RX buffer, configures 8N1 with no flow control and `UART_SCLK_DEFAULT`, and sets TX/RX pins (no RTS/CTS). It holds a single static port, so only one instance is supported at a time.

#### `ys_rfid2_hal_uart_config_t`
```c
typedef struct {
  int port;
  int baud_rate;
  int tx_pin;
  int rx_pin;
} ys_rfid2_hal_uart_config_t;
```

#### API
```c
esp_err_t ys_rfid2_hal_uart_init(const ys_rfid2_hal_uart_config_t *config);
void      ys_rfid2_hal_uart_deinit(void);
int       ys_rfid2_hal_uart_read(uint8_t *out_data, size_t len, uint32_t timeout_ms);
esp_err_t ys_rfid2_hal_uart_write(const uint8_t *data, size_t len);
void      ys_rfid2_hal_uart_flush(void);
```

- `ys_rfid2_hal_uart_init`: returns `ESP_OK`, `ESP_ERR_INVALID_ARG` if `config` is NULL, `ESP_ERR_INVALID_STATE` if already initialized, or a propagated `uart_*` error.
- `ys_rfid2_hal_uart_read`: wraps `uart_read_bytes`. Returns the number of bytes read, or -1 on error / not initialized.
- `ys_rfid2_hal_uart_write`: wraps `uart_write_bytes`. Returns `ESP_OK`, `ESP_ERR_INVALID_STATE` if not initialized or `data` is NULL, or `ESP_FAIL` on write error.
- `ys_rfid2_hal_uart_flush`: flushes the UART input buffer.

## Parser Layer (`ys_rfid2_parser.h`)

#### `ys_rfid2_parser_reset`
```c
void ys_rfid2_parser_reset(void);
```
Discards accumulated bytes and resets the line position.

#### `ys_rfid2_parser_feed`
```c
bool ys_rfid2_parser_feed(uint8_t byte, ys_rfid2_raw_data_t *out_raw);
```
Feeds one byte. Returns `true` and fills `out_raw` when a complete, valid `"card number: xxxxxxxxxx@"` frame is parsed; otherwise `false`.

## Usage Example

```c
static void on_card(const ys_rfid2_event_t *ev, void *ctx) {
  if (ev->type == YS_RFID2_EVENT_CARD_DETECTED) {
    ESP_LOGI("APP", "Card: %s", ev->raw.id_str);
  } else {
    ESP_LOGI("APP", "Card removed");
  }
}

void app(void) {
  ys_rfid2_config_t cfg = ys_rfid2_default_config();
  ESP_ERROR_CHECK(ys_rfid2_init(&cfg));
  ESP_ERROR_CHECK(ys_rfid2_start(on_card, NULL));
  // ...
  ys_rfid2_stop();
  ys_rfid2_deinit();
}
```
