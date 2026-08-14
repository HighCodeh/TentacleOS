# OTA Update Service

Handles firmware updates for TentacleOS via MicroSD card. Uses A/B OTA partitions with automatic rollback and dual-chip synchronization (ESP32-P4 + ESP32-C5).

## How It Works

The C5 firmware is embedded inside the P4 binary at build time. A single `.bin` file updates both chips.

### Update Flow

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

Version is read from `assets/config/OTA/firmware.json`. Both P4 and C5 share the same version string. The C5 responds its version via `SPI_ID_SYSTEM_VERSION` (0x04).

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
