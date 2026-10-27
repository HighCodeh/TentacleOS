<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# FatFS exFAT build override

P4 uses the FatFS component shipped with ESP-IDF **v6.1**, plus the reviewed
patch in `firmware_p4/components/Service/fatfs/exfat.patch`. The repository no
longer carries a second copy of the component. C5 keeps its SDK configuration.

During CMake configuration, `tools/fatfs/prepare.py` checks the SDK version file,
all 47 component inputs, the patch and the patched outputs against
`patch_manifest.json`. It copies the SDK sources and applies the patch in an
isolated temporary directory, then publishes the result beneath
`<build>/components/Service/fatfs`. That generated directory is registered via
`EXTRA_COMPONENT_DIRS` under the name `fatfs`, overriding the SDK component.
The SDK, `managed_components/` and tracked patch directory are never patched.
No repository is cloned and no network access is required for this preparation.

Unchanged generated files retain their timestamps on subsequent configuration.
Missing or changed SDK inputs and rejected patches stop configuration; there is
no fallback to an unpatched FAT-only component. Independent build directories
have independent copies, including `build/`, `build_highboy/` and the isolated
factory-security review. Generated output is disposable and may be deleted with
its build directory. Source edits belong in the patch, never in generated files.

## Preserved adaptations

Comparison with the original v5.5.3 copy identified changes in three files:

- `src/ffconf.h`: enable `FF_FS_EXFAT` and define `FF_USE_LABEL` as zero when its
  Kconfig option is disabled. Long-file-name heap support remains required.
- `vfs/esp_vfs_fat.h`: declare explicit exFAT formatting and mount-state queries.
- `vfs/vfs_fat_sdmmc.c`: keep `FM_ANY` for existing format calls and use
  `FM_EXFAT` for `esp_vfs_fat_sdcard_format_exfat()`. Validate the registered
  context and allocate scratch memory before detaching the filesystem. After
  formatting, attempt one non-formatting remount and retain the card/context on
  failure. `esp_vfs_fat_sdcard_is_mounted()` reports the retained mount state.

The rest comes from IDF v6.1, including FatFs R0.16 and its current VFS/block-device
integration. Upstream copyright and license notices are retained in the copied
sources. Patch provenance and the previous imported-source evidence remain
reviewable in Git and `integration/import.json`.

## Validation and SDK updates

```sh
python tools/fatfs/test_fatfs.py --idf "$IDF_PATH"
bash tools/build.sh p4
```

The host test uses the actual generated `ff.c` and `ffunicode.c` with a RAM-only
block device. It formats exFAT, FAT32 and FAT, creates a long UTF-8 filename,
writes data, unmounts/remounts, reads, renames and deletes it under ASan/UBSan.
Separate lifecycle tests inject allocation, format and remount failures into
the patched VFS format functions. Generator checks cover source/patch tampering,
missing files, version mismatch, repeat configuration and isolated builds.
These tests do not access an SD card or establish physical card compatibility.

For an SDK update, compare the new component with the pinned source, rebase the
three-file patch, review the retained failure paths, and update both source and
output hashes. Run the host tests and P4 build before accepting the new manifest.
Do not remove the hash checks merely to accept an SDK change.
