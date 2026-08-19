# I2C Master Bus Init

This component brings up the shared I2C master bus used by every on-board I2C
device (charger, haptic, LED, etc.). The two firmwares are at different stages of
the ESP-IDF I2C driver migration: P4 uses the new `driver/i2c_master.h` bus-object
API and adds stuck-bus recovery (at init and reactively at runtime), while C5 still
uses the legacy `driver/i2c.h` driver with a simpler init and no recovery.

# P4

Shared I2C master bus on the new `i2c_master.h` driver, with stuck-bus recovery.

## Overview

- **Location:** `components/Drivers/i2c_init/`
- **Header:** `include/i2c_init.h`
- **Driver:** `driver/i2c_master.h` (new bus-object API)
- **Port:** `I2C_NUM_0`
- **Pins:** SDA `GPIO_I2C_SDA_PIN` (GPIO 31), SCL `GPIO_I2C_SCL_PIN` (GPIO 30), from `pin_def.h`
- **Speed:** `I2C_MASTER_FREQ_HZ` = 100 kHz (standard mode)
- **Dependencies:** `pins`, `driver/gpio`, `esp_rom_sys`

## Bus / Hardware Notes

The bus runs at 100 kHz standard mode because the V2 board pulls SDA/SCL up with
10k (R12/R13/R14 to 3.3VF) plus 22R series (R9/R10): too weak for 400 kHz
fast-mode (rise time can't reach VIH in a bit period), which is why the BQ25896
NACKs at 400 kHz. Bus config uses `I2C_CLK_SRC_DEFAULT`, `glitch_ignore_cnt = 7`,
and internal pull-ups enabled.

## Recovery Mechanisms

**Stuck-bus recovery at init.** Before the I2C driver claims the pins, `init_i2c()`
runs `bus_recover()`: it drives SCL as open-drain and reads SDA. If a slave is
holding SDA low (stuck mid-transfer after a partial reset or power glitch), it
bit-bangs up to `I2C_RECOVER_CLOCKS` (9) SCL pulses at ~100 kHz
(`I2C_RECOVER_HALF_US` = 5 us half period) to walk the slave past its ACK, then
issues a STOP (SDA rising while SCL is high). It is a no-op when SDA is already
released. This keeps a stuck bus from leaving the charger/haptic/LED unreachable
for the whole session.

**Reactive recovery at runtime.** Consumers that see repeated transfer failures
call `i2c_bus_recover()`, which calls `i2c_master_bus_reset()` on the shared bus
and increments the recovery counter. `i2c_recover_count()` exposes that counter
for `sys_monitor` health reporting.

## API Reference

#### `init_i2c`
```c
esp_err_t init_i2c(void);
```
Runs the init-time stuck-bus recovery sequence, then creates the I2C master bus on
`I2C_NUM_0`. Returns `ESP_OK`, or the failing `esp_err_t`.

#### `i2c_get_bus`
```c
i2c_master_bus_handle_t i2c_get_bus(void);
```
Returns the shared master bus handle; each device driver adds itself to it via
`i2c_master_bus_add_device()`.

#### `i2c_bus_recover`
```c
esp_err_t i2c_bus_recover(void);
```
Resets a wedged bus at runtime (reactive recovery) and increments the recovery
counter. Returns `ESP_ERR_INVALID_STATE` if the bus is not initialized, otherwise
the result of `i2c_master_bus_reset()`.

#### `i2c_recover_count`
```c
uint32_t i2c_recover_count(void);
```
Number of runtime bus recoveries performed since boot, for `sys_monitor` health.

---

# C5

Shared I2C master bus on the legacy `driver/i2c.h` driver. No bus recovery and no
recovery counter; the bus handle is not exposed.

## Overview

- **Location:** `components/Drivers/i2c_init/`
- **Header:** `include/i2c_init.h`
- **Driver:** `driver/i2c.h` (legacy driver)
- **Port:** `I2C_NUM_0`
- **Pins:** SDA `GPIO_I2C_SDA_PIN` (GPIO 8), SCL `GPIO_I2C_SCL_PIN` (GPIO 9), from `pin_def.h`
- **Speed:** `I2C_MASTER_FREQ_HZ` = 400 kHz (fast mode)
- **Dependencies:** `pins`, `driver/i2c`

## Configuration

Configured as `I2C_MODE_MASTER` with SDA/SCL internal pull-ups enabled and a
400 kHz clock. Init calls `i2c_param_config()` then `i2c_driver_install()` with no
RX/TX buffers.

## API Reference

#### `init_i2c`
```c
void init_i2c(void);
```
Configures and installs the legacy I2C master driver on `I2C_NUM_0`. Logs and
returns early on failure; no value is returned.
