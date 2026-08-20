# OTA Update Service

Handles firmware updates for TentacleOS via MicroSD card. Uses A/B OTA partitions with automatic rollback and dual-chip synchronization (ESP32-P4 + ESP32-C5).

## How It Works

The P4 and C5 update through **two separate flows**:

- **P4 self-OTA** - the P4 flashes its own inactive OTA slot from an image on the
  SD card, then reboots and self-validates (this document's main subject).
- **C5 app OTA** - a separate flow (`c5_flasher`) reads
  `/sdcard/c5/TentacleOS_C5.bin` and pushes it to the C5 over the SPI bridge; the
  C5 writes its own inactive slot and reboots. See [C5 App OTA](#c5-app-ota-over-the-spi-bridge).

> **Note:** embedding the C5 firmware inside the P4 binary now applies **only** to
> the ROM-download recovery path (used to reflash a bricked/blank C5 over UART/ROM);
> the normal C5 app update is the separate SPI-bridge flow above, not a single
> combined `.bin`.

### Update Flow (P4 self-OTA)

1. Place firmware at `/sdcard/update/tentacleos.bin`
2. Trigger `ota_start_update()` from UI or console
3. P4 validates the file and writes it to the inactive OTA partition
4. P4 reboots into new firmware
5. After `kernel_init`, `ota_post_boot_check()` confirms the image using **local**
   health: the assets LittleFS is mounted and the LVGL renderer is advancing
6. If healthy, the update is confirmed (`esp_ota_mark_app_valid_cancel_rollback`)
   and `firmware.json` is synced to the running version
7. If the local check fails, the image is left unconfirmed and the bootloader
   rolls back to the previous good image on the next reboot

> **Watchdog:** the write loop (`fread` from LittleFS + `esp_ota_write`, both
> cache-disabling) yields with `vTaskDelay(1)` per chunk so the idle task keeps
> feeding the Task Watchdog. This is required because `CONFIG_ESP_TASK_WDT_PANIC=y`
> is enabled: without the yield, writing a multi-MB image would starve the idle
> task and reboot the device mid-write. The C5 UART flash loop in `c5_flasher`
> yields the same way.

### Rollback

The system uses two app partitions (`ota_0` / `ota_1`). After OTA, the new
firmware must call `esp_ota_mark_app_valid_cancel_rollback()` to confirm.
Confirmation is gated on **local, mandatory** criteria only (assets partition
mounted + LVGL renderer alive), **never** on an optional peripheral.

> Previously confirmation was gated on `bridge_manager_init()` (the C5 bridge),
> which is disabled on purpose - so every update stayed in `PENDING_VERIFY` and
> the device rolled back forever. The C5 sync is no longer part of validation. A
> C5 version sync, if reintroduced, must be an optional step that only logs a
> warning and never blocks confirmation.

Scenarios:
- **P4 crashes before confirmation** - automatic rollback to previous firmware
- **New image cannot mount assets or the renderer is frozen** - local check
  fails, P4 does not confirm, bootloader rolls back
- **Healthy new image** - confirmed within a few seconds of boot

### C5 App OTA (over the SPI bridge)

The normal C5 firmware update is independent of the P4 self-OTA above. It is
driven from the P4 by `c5_flasher` (`components/Service/c5_flasher/c5_flasher.c`):

1. The image is read from `/sdcard/c5/TentacleOS_C5.bin` (not embedded in the P4
   binary).
2. The P4 sends `SPI_ID_SYSTEM_OTA_BEGIN` (size + transport) over the SPI bridge.
   The C5 erases its inactive OTA slot asynchronously and reports `READY` via
   `SPI_ID_SYSTEM_OTA_STATUS`.
3. The P4 streams the image as `SPI_ID_SYSTEM_OTA_DATA` chunks; the C5 writes each
   sequentially and acks. `bytes_written` (from the STATUS poll) is the resync
   point if a chunk ack is lost.
4. When the last chunk lands, the C5 finalizes, sets the boot slot to `DONE`, and
   reboots into the new firmware. The P4 marks the bridge link down so the link
   monitor re-probes and reconnects the new C5.

> On the HighBoy V2 the transport is **SPI** (`SPI_OTA_TRANSPORT_SPI`); the UART
> transport path in `c5_flasher` is kept for a future board that routes a real
> P4->C5 UART and does not work on V2.

**C5-side rollback (validated by P4 bridge health).** On the C5, after a fresh OTA
image boots, `ota_post_boot_check()`
(`firmware_c5/components/Service/ota/include/ota_service.h`) sees the running
partition is pending verification and **waits for the P4 to reach it over the
bridge** before marking the app valid; if the P4 does not establish the link
within the validation window, the C5 does not confirm and the bootloader rolls
back to the previous C5 image. It must be called after the C5's `kernel_init` so
the bridge slave is already listening. This is the inverse of the P4 side, whose
confirmation depends only on **local** health (see below) and never on the C5.

### Partition Table

See [boot_report](../boot_report/README.md) for the full current layout (the OTA
slots were resized to `0x270000` to make room for a `coredump` partition).

| Name | Type | Size |
|---|---|---|
| ota_0 | app | 0x270000 |
| ota_1 | app | 0x270000 |
| coredump | data | 64K |
| otadata | data | 8K |

### Versioning

The build-time version string is `common/metadata/version_info.txt` (a single
line, e.g. `1.4.0`). `firmware_p4/components/Service/CMakeLists.txt` reads it and
configures `common/metadata/ota_version.h.in` into a generated `ota_version.h`
that defines `FIRMWARE_VERSION`, which `ota_service.c` compiles in and returns
from `ota_get_current_version()`. (The header is generated at build time from
`version_info.txt`; there is no checked-in `ota_version.h`.)

`assets/config/OTA/firmware.json` is **only the runtime/synced copy**, not the
source of truth: `firmware_p4/CMakeLists.txt` stamps the same `version_info.txt`
value into the assets image at build time, and at boot `ota_sync_version_to_assets()`
(called from `ota_post_boot_check()`) rewrites `firmware.json` to the running
`FIRMWARE_VERSION` if they differ.

The C5 reports its own version over the SPI bridge via `SPI_ID_SYSTEM_VERSION`
(SYSTEM subcommand `0x04`).

## API

```c
bool ota_update_available(void);
esp_err_t ota_start_update(ota_progress_cb_t progress_cb);
esp_err_t ota_post_boot_check(void);
const char* ota_get_current_version(void);
ota_state_t ota_get_state(void);
```

### Progress Callback

```c
void on_progress(int percent, const char *message) {
    // 0-5%: Validating
    // 5-90%: Writing to flash
    // 90-95%: Finalizing
    // 95%: Rebooting
}

ota_start_update(on_progress);
```

### Post Boot Check

Must be called from `main.c` **after** `kernel_init()`, so the assets partition
and LVGL are up for the local health check (it briefly waits on the render beat):

```c
kernel_init();
ota_post_boot_check();
```

## sdkconfig

Required:
```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

## Dependencies

- `app_update` (esp_ota_ops)
- `storage_assets` (firmware.json + local health: assets mounted)
- `ui_liveness` (local health: LVGL render beat)
- `sd_card_init` (SD mount status)
- `cJSON` (JSON parsing)
