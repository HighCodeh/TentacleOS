# C5 Flasher Service - P4 Master

Lets the ESP32-P4 update or recover the ESP32-C5 firmware. There are two paths:

- **App OTA (primary):** the C5 keeps running its app; the P4 hands it a new
  image, which it writes to its inactive OTA slot and boots. The control plane
  (begin / status / per-chunk acks) always rides the SPI bridge; the image bytes
  travel over the transport the caller selects. The image is normally **streamed
  from the SD card** (`/sdcard/c5/TentacleOS_C5.bin`), so it is no longer embedded
  in the P4 binary for the everyday update.
- **ROM serial-flash (fallback / bricked C5):** the P4 speaks the ROM
  serial-bootloader protocol over UART itself (via `esp-serial-flasher`) to a C5
  that is in ROM download mode. Only this path uses the **embedded** C5 images
  (`c5_rom_flasher.c`, guarded by `C5_ROM_IMAGES_EMBEDDED`).

## App OTA

```c
esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size, uint8_t transport);
```

- `bin_data == NULL` -> the image is streamed from `/sdcard/c5/TentacleOS_C5.bin`
  (`bin_size` is taken from the file). Otherwise the caller's buffer is used.
- `transport` is a `spi_ota_transport_t` selector:
  - `SPI_OTA_TRANSPORT_SPI` - image bytes arrive as `SPI_ID_SYSTEM_OTA_DATA`
    chunks (240 firmware bytes per chunk). No UART wiring needed.
  - `SPI_OTA_TRANSPORT_UART` - image bytes arrive raw over UART1 -> C5 UART0
    (needs `c5_flasher_init()`). **Non-functional on the HighBoy V2** (no direct
    P4<->C5 UART on that board); use SPI there.

Flow (`c5_flasher_update`):
1. Send `SPI_ID_SYSTEM_OTA_BEGIN` with `spi_ota_begin_t { size, transport }`. The
   BEGIN ack often coincides with the C5's erase and is lost, so the P4 confirms
   the C5 actually started by polling `SPI_ID_SYSTEM_OTA_STATUS`
   (`spi_ota_status_t { state, bytes_written }`) instead of trusting the ack.
2. Poll `OTA_STATUS` until the C5 reaches `SPI_OTA_STATE_READY` (erase done).
3. Stream the image in blocks (SPI: 240 B `OTA_DATA` chunks; UART: 4 KB blocks),
   advancing the live progress counters read by `c5_flasher_progress()`.
4. The C5 validates, sets the new slot to boot and reboots. A failed transfer is
   harmless: the old slot still boots.

`spi_ota_state_t` progression: `IDLE -> ERASING -> READY -> RECEIVING -> DONE`
(or `ERROR`).

## ROM download entry points

For a blank or bricked C5 (no working app / bridge), reflash over the ROM
bootloader:

```c
esp_err_t c5_flasher_enter_download(void); // ask the running C5 to reboot into ROM download mode
esp_err_t c5_flasher_rom_flash(void);      // reflash from embedded images
void      c5_passthrough_run(void);         // forward host esptool <-> C5 ROM (never returns)
```

- `c5_flasher_enter_download()` sends `SPI_ID_SYSTEM_ENTER_DOWNLOAD` over the SPI
  bridge; the C5 acks and reboots into ROM serial-download mode. After this the
  C5 is no longer running its app (the OTA receiver is gone), so recovery must
  continue via `c5_flasher_rom_flash()` or passthrough.
- `c5_flasher_rom_flash()` writes bootloader + partition table + otadata + app -
  all embedded in the P4 - and MD5-verifies each region. **Precondition:** the C5
  must already be in ROM download mode (strap it manually, or call
  `c5_flasher_enter_download()` first). Returns `ESP_ERR_NOT_FOUND` unless
  `C5_ROM_IMAGES_EMBEDDED` is set.
- `c5_passthrough_run()` bridges the P4's console UART (host PC USB-serial) to the
  C5 UART0 so the host can run `esptool` directly against the C5 ROM bootloader.
  Kills the REPL, disables UART logs and never returns; reboot the P4 (or press
  BACK) to exit.

## UART helpers

```c
esp_err_t c5_flasher_init(void);        // bring up UART1 (only needed for the UART OTA transport)
void      c5_flasher_release_uart(void); // delete UART1 + tri-state the C5-UART pins for an external programmer
```

## Bridge / status helpers

```c
esp_err_t c5_flasher_ping(void);  // SPI_ID_SYSTEM_PING
esp_err_t c5_flasher_info(void);  // SPI_ID_SYSTEM_INFO -> chip model / rev / MAC / free heap
esp_err_t c5_flasher_sync(void);  // re-probe the C5 and mark the bridge alive iff it answers a ping
void      c5_flasher_progress(uint32_t *sent, uint32_t *total); // live OTA progress for the UI bar
```

## Callers

`bridge_manager` triggers the app OTA (`bridge_manager_force_update()` calls
`c5_flasher_update(NULL, 0, SPI_OTA_TRANSPORT_SPI)`). Version mismatch is
**detection-only** - the user starts the update explicitly (the `c5` console
command); the P4 never auto-flashes. See
[`../bridge_manager/README.md`](../bridge_manager/README.md).

## Symbols

The ROM-flash fallback references the embedded C5 app image via:
- `_binary_TentacleOS_C5_bin_start`
- `_binary_TentacleOS_C5_bin_end`

## Build Automation

Use `./tools/build_and_flash.sh` to keep the C5 binary current (SD image and, for
the ROM fallback, the embedded copy) before flashing the P4.
