# SPI Bus Driver

This component acts as a central manager for the SPI bus, allowing multiple devices (Display, Radio, SD Card) to share the same SPI host safely and efficiently.

## Overview

- **Location:** `components/Drivers/spi/`
- **Header:** `include/spi.h`
- **Dependencies:** `driver/spi_master`
- **Host:** `SPI3_HOST`

## Supported Devices (`spi_device_id_t`)

1.  **SPI_DEVICE_ST7789:** Display Driver
2.  **SPI_DEVICE_CC1101:** Sub-GHz Radio
3.  **SPI_DEVICE_SD_CARD:** Storage

## API Reference

### `spi_init`
```c
esp_err_t spi_init(void);
```
Initializes the SPI bus (MOSI, MISO, SCLK) on `SPI3_HOST` using DMA Channel `Auto`.
- **Pins:** Defined in `pin_def.h`.
- **Max Transfer Size:** 32768 bytes.

### `spi_add_device`
```c
esp_err_t spi_add_device(spi_host_device_t host, spi_device_id_t id, const spi_device_config_t *config);
```
Adds a specific device to the initialized bus.
- **host:** SPI host device (SPI2_HOST, SPI3_HOST).
- **id:** Device identifier enum.
- **config:** Struct containing CS pin, clock speed, SPI mode, and queue size.

### `spi_get_handle`
```c
spi_device_handle_t spi_get_handle(spi_device_id_t id);
```
Retrieves the ESP-IDF `spi_device_handle_t` for a registered device ID. Useful for calling native ESP-IDF SPI functions.

### `spi_transmit`
```c
esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len);
```
Performs a simple polling/blocking transmission to the specified device.
- **Note:** For high-performance display flushing, specific drivers (like `esp_lcd`) typically use their own transmission logic using the handle obtained via `spi_get_handle`.

### `spi_deinit`
```c
esp_err_t spi_deinit(void);
```
Removes all devices and frees the SPI bus resources.
