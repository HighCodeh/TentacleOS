# OTA Update Service

Handles independent P4/C5 application updates. The integrated High Boy profile
uses its existing A/B app slots; neither app update restores bootloaders, assets
or an erased device. See the [deployment contract](../highboy/README.md#flash-and-storage-boundary).

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

See [boot_report](../boot_report/README.md) for the integrated current layout.
P4 app slots stay at `0x2D0000`; C5 app slots stay at `0x200000`. No map migration
or generic full-flash procedure is part of an app update.

| Name | Type | Size |
|---|---|---|
| P4 ota_0 | app | 0x2D0000 |
| P4 ota_1 | app | 0x2D0000 |
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


## Native P4 update screen and failure handling

The developer P4 Update screen checks the actual SD file on a worker pinned to
the radio core and displays the installed runtime version. It no longer invents
a new version, advances progress on a timer or reboots without an update. OK on
an available image invokes `ota_start_update`; progress/errors are copied into
a bounded snapshot and rendered by the UI timer. Probe/write workers never touch
LVGL. While writing/rebooting, input cannot launch another update. Deleting a
screen removes its timer; an in-flight native operation owns no screen pointers.
Task/timer failures show an error and do not report a successful update.

The P4 service serializes update calls and holds wake power during preflight and
writes. It checks seek/size/partition conditions, refuses the running partition,
allocates the chunk before beginning flash operations, and rejects short/error
reads. SDK finalization must succeed before selecting the new boot partition.
On error the file/buffer are closed/freed. Failed abort retains the handle and
wake hold; a later call retries cleanup before new work. A failed power release
also retains its hold. The successful reboot path retains wake power until reset.

Both targets propagate OTA-state read errors, treating only `ESP_ERR_NOT_FOUND`
as absent OTA metadata. SDK confirmation errors are returned and logged instead
of announcing acceptance. P4 version metadata is synced only after successful
confirmation (or an ordinary boot); metadata write failure is logged and does
not undo a valid image. C5 confirmation still needs P4 bridge activity; timeout
requests rollback and reports a failed rollback API call. Physical rollback and
SD/flash behavior remain to be validated on an installed device.

`python3 tools/ota/test_ota.py` runs the actual P4 service, actual UI and extracted
C5 confirmation function with ASan/UBSan and explicit file/flash/OS/LVGL fixtures.
It tests SD/partition/allocation/read/write/finalization failures, exclusivity,
retained cleanup and retry, health/confirmation/rollback errors, task/timer
failures and deletion-safe UI callbacks. These fixtures never flash a device.
