# Recovery: Safe Mode & Factory Reset

Field-recovery path for the ESP32-P4 when the normal UI cannot be trusted (a
broken theme, inconsistent config, or a required subsystem that failed to come
up). Reachable without any host tooling.

## Entering safe mode

Safe mode is entered at power-on in two ways, both decided in `kernel_init`
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

In safe mode the kernel brings up only LED, battery, display, LVGL and the
recovery UI. Radios (CC1101, the C5 bridge, RFID), Wi-Fi, host link, console and
the SD custom theme are **not** started. `kernel_init` returns `esp_err_t`
(`ESP_FAIL` when it fell back due to a required failure); `main.c` logs the
degraded boot.

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
never stalls the LVGL renderer, then the device reboots.

## Factory reset semantics

Implemented in `tos_factory_reset.{c,h}` (see
[storage_api](../storage_api/README.md#factory-reset-tos_factory_reseth)):

- **Reset config** - deletes config files on both storages + the first-boot
  marker; defaults are re-seeded on the next boot. Keeps NVS, loot and assets.
- **Reset all** - config + user captures/loot/themes/scripts on both storages +
  `nvs_flash_erase`. Never formats the `assets` partition (the shipped
  icons/html/fonts are exactly 100% of the partition and would need a re-flash)
  and never deletes the on-SD C5 firmware image.

Deletion is file-only (directory skeleton preserved) so the first config save
after a reset still finds its folder.

## Notes

- If the `assets` partition itself failed to mount, safe mode still comes up but
  may render without icons; the boot map and crash text still show.
- `power_policy_is_asleep()` gates the input router, so the wake press only wakes
  the screen - this is unrelated to safe mode but shares the input path.
