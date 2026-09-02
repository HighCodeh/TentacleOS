# fatfs (vendored, exFAT-enabled)

Local override of the ESP-IDF `fatfs` component. It shadows the built-in one so
project builds, teammates, and CI all pick up exFAT without touching the shared
IDF install.

It lives nested under `components/Service/` (not at the top-level `components/`)
and is registered as a component through `EXTRA_COMPONENT_DIRS` in the project
`CMakeLists.txt`, the same way `Applications/gameboy` and `Applications/doom` are.
The name stays `fatfs`, which is what makes it override the IDF built-in.

## Why it exists

ESP-IDF does not expose an exFAT toggle in menuconfig, on purpose: enabling exFAT
may require a Microsoft license, so Espressif keeps it off and tells users to patch
`ffconf.h` themselves (esp-idf issue #6601). SDXC cards (>32 GB) ship formatted
exFAT, so without this they will not mount and would be reformatted down to FAT32.

## The deltas vs upstream

Copied verbatim from `esp-idf v5.5.3` (`components/fatfs`), minus the test dirs
(`test_apps/`, `test_fatfsgen/`, `host_test/`), with three changes:

1. `src/ffconf.h`: `FF_FS_EXFAT` `0` -> `1` (the actual exFAT enable).
2. `src/ffconf.h`: `FF_USE_LABEL` guarded so it is `0` when `CONFIG_FATFS_USE_LABEL`
   is unset, instead of expanding to the undefined macro. Enabling exFAT compiles a
   `dir_read` path that references `FF_USE_LABEL`; with the label config off (our
   default) the stock line `#define FF_USE_LABEL CONFIG_FATFS_USE_LABEL` fails to
   build. Alternatively you could set `CONFIG_FATFS_USE_LABEL=y` and drop this.
3. `vfs/vfs_fat_sdmmc.c` + `vfs/esp_vfs_fat.h`: added
   `esp_vfs_fat_sdcard_format_exfat()`. The stock `esp_vfs_fat_sdcard_format*`
   only does `FM_ANY`, which keeps a small card on FAT32; the new entry point
   forces `FM_EXFAT`. Implemented by factoring the existing format body into a
   static `format_card_fm(..., BYTE fmt_kind)` (behavior for existing callers is
   unchanged, still `FM_ANY`). Re-sync: re-apply this same factor + entry point.

Preconditions were already met by our sdkconfig: `FF_USE_LFN >= 1`
(`CONFIG_FATFS_LFN_HEAP`), `FF_FS_MINIMIZE 0`, a valid code page.

## Re-syncing on an IDF bump

Diff this dir against `$IDF_PATH/components/fatfs` and re-apply the one line above:

    diff -r $IDF_PATH/components/fatfs src/ffconf.h ...

Keep the delta to `FF_FS_EXFAT` only. To also format cards as exFAT, pass
`FM_EXFAT` (or `FM_ANY`) to `f_mkfs` in the SD Format path.
