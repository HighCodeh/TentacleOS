# OTA Update Service

Handles firmware updates for TentacleOS via MicroSD card. Uses A/B OTA partitions with automatic rollback and dual-chip synchronization (ESP32-P4 + ESP32-C5).

## How It Works

The C5 firmware is embedded inside the P4 binary at build time. A single `.bin` file updates both chips.

### Update Flow

1. Place firmware at `/sdcard/update/tentacleos.bin`
2. Trigger `ota_start_update()` from UI or console
3. P4 validates the file and writes it to the inactive OTA partition
4. P4 reboots into new firmware
5. On boot, `ota_post_boot_check()` verifies the C5 is in sync
6. If C5 version differs, `c5_flasher` updates it via UART
7. If everything is OK, the update is confirmed
8. If anything fails, the bootloader rolls back automatically

### Rollback

The system uses two app partitions (`ota_0` / `ota_1`). After OTA, the new firmware must call `esp_ota_mark_app_valid_cancel_rollback()` to confirm. If it doesn't (crash, C5 flash failure, etc.), the bootloader reverts to the previous partition on the next reboot.

Scenarios:
- **P4 crashes before confirmation** — automatic rollback to previous firmware
- **C5 flash fails** — P4 does not confirm, rollback restores both chips
- **C5 flash interrupted (power loss)** — C5 ROM bootloader is always accessible, P4 re-flashes on next boot
- **Rollback after C5 was already updated** — rolled-back P4 contains old C5 binary, version mismatch triggers re-flash

### Partition Table

| Name | Type | Size |
|---|---|---|
| ota_0 | app | 4MB |
| ota_1 | app | 4MB |
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

Must be called early in `main.c` before `kernel_init()`:

```c
ota_post_boot_check();
```

## sdkconfig

Required:
```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

## Dependencies

- `app_update` (esp_ota_ops)
- `bridge_manager` (C5 version check and flash)
- `storage_assets` (firmware.json)
- `sd_card_init` (SD mount status)
- `cJSON` (JSON parsing)
