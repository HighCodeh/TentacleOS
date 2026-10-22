# Recovery: Safe Mode & Factory Reset

Field-recovery path for the ESP32-P4 when the normal UI cannot be trusted (a
broken theme, inconsistent config, or a required subsystem that failed to come
up). Reachable without any host tooling.

## Entering safe mode

Safe mode is entered at power-on in three ways, both decided in `kernel_init`
before radios, custom themes and services start, so recovery always comes up
minimal:

1. **Button combo** - hold **OK + BACK** together while powering on. Detection
   (`detect_safe_mode_combo`) waits ~250 ms for the input sampler to debounce,
   then requires the combo to stay held across a ~500 ms confirm window so a
   stray press never triggers it.
2. **Required-subsystem failure** - if a required boot stage failed (see
   [boot_report](../boot_report/README.md)), `kernel_init` drops into safe mode
   automatically instead of booting blind. This is the degraded-mode target.
3. **Boot loop** - after 3 consecutive abnormal boots (panic / watchdog /
   brownout), boot-loop detection (see
   [boot_report](../boot_report/README.md#boot-loop-detection-item-4)) forces
   safe mode and the footer shows the reset reason. This breaks the endless
   reboot cycle from marginal hardware and gives a factory-reset path out.

Recovery is selected before SD startup, first-boot seeding, configuration
loading or custom themes. The kernel brings up only the base buses/input, assets
mount, LED, charger, display, LVGL and recovery UI. LF outputs are initialized
low without energizing the antenna. Radios (CC1101, the C5 bridge, RFID), Wi-Fi, host link, console and
the SD custom theme are **not** started. `kernel_init` returns `esp_err_t`
(`ESP_FAIL` for every recovery entry); `main.c` skips OTA confirmation and
asset-version synchronization on this path. NVS init failures never trigger an
automatic erase. If the display fails its single retry or LVGL fails, no input
device/UI call follows; recovery stays headless, preserves diagnostics and
starts the checked system monitor with render supervision disabled. Required
graphics startup is checked before normal radios/audio/services are started.

## The recovery menu

`ui_init_safe_mode` builds a minimal UI (theme + input pump + render heartbeat,
no boot animation, no power policy) and loads the safe-mode screen. Menu:

| Item | Action |
|------|--------|
| Reset config | Confirm, then `tos_factory_reset_config` + reboot |
| Reset all | Confirm, then `tos_factory_reset_all` + reboot |
| View last crash | Opens the [crash viewer](../boot_report/README.md) |
| View boot map | Opens the [boot-map viewer](../boot_report/README.md) |
| Reboot | `esp_restart` |

The reset actions run a two-step confirm inside the screen's own input handler
(not `msgbox`, which polls buttons directly and would double-fire against the
central input router). The wipe itself runs in a separate task so file deletion
never stalls the LVGL renderer. The device reboots only after a successful
reset. Worker-creation and reset failures show a retryable error with BACK to
return; failed UI-lock acquisition retains an error for the next input event.

## Factory reset semantics

Implemented in `tos_factory_reset.{c,h}` (see
[storage_api](../storage_api/README.md#factory-reset-tos_factory_reseth)):

- **Reset config** - deletes config files on both storages + the first-boot
  marker; defaults are re-seeded on the next boot. Keeps NVS, loot and assets.
- **Reset all** - config + user captures/loot/themes/scripts on both storages +
  `nvs_flash_erase`. Never formats the `assets` partition (the shipped
  icons/html/fonts are exactly 100% of the partition and would need a re-flash)
  and never deletes the on-SD C5 firmware image.

Because recovery skips SD startup, an explicit reset first attempts to mount
the SD. Mount failures or an unavailable assets filesystem return an error; a
missing SD cannot be reported as a complete two-storage wipe. Existing deletion
may have partially succeeded before a later failure. The UI leaves recovery
open on that error so the operator can check storage or reboot explicitly.

Deletion is file-only (directory skeleton preserved) so the first config save
after a reset still finds its folder.

## Notes

- If the `assets` partition itself failed to mount, safe mode still comes up but
  may render without icons; the boot map and crash text still show.
- `power_policy_is_asleep()` gates the input router, so the wake press only wakes
  the screen - this is unrelated to safe mode but shares the input path.


## Offline validation

The boot-report test runner compiles the production kernel and recovery/reset
functions with ASan/UBSan. Cases cover NVS errors without erase, boot loops,
manual recovery, missing assets, display/LVGL failure, UI gating, normal boot,
requested SD mounting, NVS erase/re-init failures, worker-creation failure and
failure without reboot. Fake HAL/scheduler/NVS results are explicit. No real
filesystem image, card, NVS partition, device or flash was changed by these tests.
