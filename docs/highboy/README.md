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

# High Boy integrated development branch

This checkout combines TentacleOS `dev` revision
`011ed7da4c750b7777fd6dbad8ce4541304c6898` with the verified local Muse pair
`ad4f84fcf97b` (P4) / `c80ba998253c` (C5). It is not the standalone PlayStation lab.
The former sources and packaged binaries remain unchanged outside this checkout.

## Ownership and layout

This is an editable source repository, not another generated `integrated_*/source`
snapshot. Edit these files directly. Do not run the old Muse `prepare.py --refresh`
against this checkout. `integration/import.json` records the one-time import,
source hashes, upstream hashes and relocated paths. The entire `integration/`
directory is local and ignored by Git; a fresh clone does not contain this audit
history, device backups or release artifacts. Build and guarded flash routing do
not depend on its presence. Factory security inputs are versioned under
`tools/highboy/security_profiles/`. The local import history is provenance, not a claim
that subsequent edits are identical to the import. Libraries migrated to the
Component Manager are no longer part of the local source-preservation inventory;
their versions are declared in the firmware manifests.

- User interfaces and emulators: `firmware_p4/components/Applications`.
- Hardware drivers: each target's `components/Drivers`.
- Muse controller, wire protocol and SDK adapters: `components/Service/muse_port`.
- P4 Muse board integration: `components/Service/muse`.
- C5 Noise and MP3 dependencies: `components/Service/noise_core` and `minimp3`.
- Resource arbitration: `firmware_p4/components/Core/resource_mgr`.
- exFAT patch metadata: `firmware_p4/components/Service/fatfs`; generated override under the build directory.
- Managed third-party dependencies: each target's `main/idf_component.yml`.

Only the four upstream architecture roots are used under `components`.
Independent nested components are registered explicitly through CMake.

The V2.03 external module header pinout and shared-signal constraints are
documented in [Pins / pin_def](../pins/README.md#v203-external-module-header).

## Merge decisions

| Area | Decision |
| --- | --- |
| DOOM | Upstream internal-DMA strips, retaining local input hygiene and LVGL lock order. |
| USB microSD | New TinyUSB storage-handle API and no automatic formatting; safe handling of deferred writes during teardown. |
| Sub-GHz | Restore upstream protocol encoders, decorated-name lookup, serialization and offline self-test; retain local RF hardware configuration. |
| Dependencies | Apply upstream LVGL `<9.6` constraint; pin managed library versions and retain DES support. |
| Muse / Grok | Preserve voice-note/PTT + text replies, official Muse menu mark, and the separate offline-only Grok animation. |
| NFC / RFID / LoRa / IR | Preserve local hardware drivers, read/write/APDU features, memory placement and UI changes rather than replacing them with older upstream implementations. |
| Wi-Fi / BLE / SPI | Preserve the local dual-band analysis, HID mouse, memory, transaction and session fixes. |
| Apps / games / power | Preserve glass-style Apps menu, covers, Game Boy controls, DOOM, brightness and display-hold behavior. |

Upstream's C5 tree is identical between the previous upstream snapshot and this
revision. Replacing our C5 with the stock image would remove Muse and regress the
local protocol. Both integrated targets retain identical SPI version **8** headers;
upstream currently has version 6. The Muse extension remains a separate category.

## Local prototype integration, 16 November 2026

This candidate integrates dev `c3f85db` in an isolated worktree with the existing
0201891B wardriving workflow and battery guard. Both firmware images must be
updated together: incremental Wi-Fi scan retains commands `0x56..0x58`, while
wardriving uses `0x59..0x5B`. Identical P4/C5 protocol headers are checked at build.
The GPS receiver remains on GPIO12 with TX on GPIO11; the diagnostic and recorder
share UART3 exclusively. The recorder lives in `Service/wardrive`; the UI stop
hook releases its worker and restores orientation.

The production profile uses the upstream CPU clock selection and 200 MHz PSRAM,
with no early startup clock override. Watchdog, brownout and battery charge guards
remain enabled. The battery screen does not present an unqualified gauge percentage.

Both chips on unit 0201891B were migrated from `0x2D0000` app slots to the
upstream 3 MiB slots, with complete flash backups and verified programmed bytes.
Bootloaders and NVS were preserved; rebuilt P4 assets retain twelve readable
user configuration files. The old unreadable C5 assets remain in the raw backup;
the installed C5 filesystem uses packaged defaults. The microSD was not formatted
or migrated. Both applications booted and the paired SPI ping passed. P4 mounted
the microSD and read a physical NFC card. These checks do not establish complete
RF compatibility or long-term stability.

An app-only P4 update subsequently installed the Type 2 short-reply correction,
with unchanged flash regions verified. ROM flashing on this P4 used 230400 baud,
bounded 64 KiB writes and explicit 8 MiB flash parameters before range digests;
the RAM stub and USB automatic reset were not reliable in this session.
During that deployment the ADC was at its floor and the gauge was unavailable,
so the charge guard remained active. Later live telemetry recovered: 3.42–3.56 V
at the battery and approximately 350 mA charging, with a 384 mA programmed limit,
4.176 V regulation and no reported charger faults. The gauge still reports an
unqualified 3000 mAh model; percentage, cell temperature and runtime remain
unvalidated. No low-voltage or thermal safeguard was bypassed.
On 2026-11-18, telemetry was again approximately 3.00 V with USB present,
zero net charging current and the low-voltage recovery guard active. Charge
requested is not equivalent to charge enabled; the UI now displays that reason.

USB Storage mounted on macOS, accepted a small NFC profile and passed
byte-for-byte readback. Type 2 target startup subsequently failed to allocate
3328 bytes of internal DMA memory, including on repeated attempts after reboot.
The short-response path now uses one RMT hardware memory block for ACK/NACK,
without that DMA allocation; Classic and NFC-V retain the full target path.
See [NFC target memory](../nfc/README.md#discovery-deadlines-and-target-dma).
Ten Hotel start/stop cycles and one additional start after screen re-entry passed
on unit 0201891B with the short-target build. No independent RF reader was
available for acceptance testing of that exact build.


## Build

Both builds also generate their own `assets.bin`. P4 uses the existing
`tools/png_to_bin/` conversion scripts, including rotated frames; C5 packages
its configuration/data directory directly. Standard `idf.py flash` now includes
the assets partition; `assets-flash` writes only that filesystem. The guarded
`tools/flash.sh --mode full` also installs assets. See [assets](../storage_assets/README.md#v203-p4-assets-build-and-flash).


```sh
bash tools/build.sh       # C5, then P4
bash tools/build.sh p4    # P4 only
bash tools/build.sh c5    # C5 only
python3 tools/highboy/check_contracts.py
bash tools/highboy/test.command
python3 tools/highboy/package.py   # rebuild, retest, then package app binaries
```

Both targets use the pinned **ESP-IDF v6.1** release. Set `IDF_PATH` to an
installed v6.1 checkout, or configure `HIGHBOY_P4_IDF_PATH` and
`HIGHBOY_C5_IDF_PATH` to that same checkout. Optional per-target
`HIGHBOY_*_TOOLS_PATH` and `HIGHBOY_*_PYTHON_ENV` variables select the installed
tools and Python environment. The wrapper accepts the standard SDK layout and
EIM's `activate_idf_v6.1.sh -e` environment output, without changing EIM's selected
installation or modifying SDK sources. No personal paths are tracked.

These variables may be placed in the ignored repository file
`.highboy-build.env` (or a file selected by `HIGHBOY_BUILD_ENV`). This file is
sourced as local shell configuration: use a trusted file containing toolchain
paths only, never account credentials. Explicit exported `HIGHBOY_*` toolchain
variables take precedence over that file. Install IDF/tools and initialize their
required submodules before building. The normal build enables the IDF Component
Manager with `IDF_COMPONENT_MANAGER=1`, allowing dependency resolution from
`idf_component.yml` into `managed_components/`. This is an environment setting,
not a `sdkconfig.defaults` option. Dependency resolution may access the network;
the build does not install SDK toolchains or Python packages.

Third-party libraries should use version-constrained manifest dependencies.
The Component Manager downloads libraries into ignored `managed_components/`
directories; no `vendor/` overrides remain. Dependency versions are fixed in
each manifest. The C5 MQTT Git revision preserves the previous source. C5 LED
strip compilation force-includes `esp_heap_caps.h` for IDF 6 compatibility
without modifying downloaded sources. Lua and MicroQuickJS stay inside
`Service/script_engine` for their restricted integration and local adaptation.
The P4 `components/Service/fatfs` directory now holds a pinned patch and manifest.
CMake generates the exFAT-enabled SDK v6.1 component inside each build directory;
see [FatFS](../fatfs/README.md). No SDK installation or managed component is edited.
The isolated factory-security review also uses the manager and the same
project manifests; first-time dependency resolution may require network access.
Before configuration, the build verifies the pinned revisions of the target's
required SDK submodules: eight shared libraries for P4 and thirteen libraries
including the radio stack for C5. ESP-IDF's generic all-target submodule download
is then disabled, so a P4 build does not fetch unused Wi-Fi/Bluetooth libraries.
Build products live in the standard `firmware_{p4,c5}/build`
directories.

P4 loads only `firmware_p4/sdkconfig.defaults`, including its installed partition
offset and development-security settings. Its flash profile is 8 MiB, DIO,
80 MHz. The V2.03 schematic identifies U8 as P25D64SH; its
[datasheet, table 5-3-2](https://datasheet.lcsc.com/datasheet/pdf/75d44ed55186df32a74d391edcf2f238.pdf)
allows 104 MHz for 2READ/DIO with four dummy cycles and requires eight for
120 MHz. IDF v6.1 uses four DIO dummy cycles and has no P25D64SH-specific
high-performance adjustment, so the experimental 120 MHz option is not enabled.
80 MHz is the highest selectable clock within that timing configuration; board
stability still requires hardware validation. This clock controls flash access
during execution, not the serial upload baud rate. Existing local `sdkconfig`
files retain prior selections: use `idf.py menuconfig` to select 80 MHz before
rebuilding. The bootloader must also be rebuilt and updated for its boot-time
clock to change; the guarded app-only updater preserves the installed bootloader.
The former `sdkconfig.muse` overlay
has been merged into that file. C5 also loads only `sdkconfig.defaults`, including its memory and scheduling
policy; the former baseline overlay has been merged into that file. Factory-security review
profiles remain separate from these normal development defaults.

The P4 app-signing trust store can be selected with `TOS_APP_TRUST_PUBLIC_KEY`
(64 public-key hex characters). `TOS_APP_PRODUCTION=ON` requires an explicit key
and rejects the shipped development identity. This does not enable Secure Boot,
Flash Encryption or provision eFuses. See [SDK trust configuration](../app_sdk/README.md#keys--trust-store).

Build only produces applications. It does not access serial ports, flash, SD,
microphone, token storage or any live Muse API. It does not regenerate runtime
assets, embed a different C5 recovery image or migrate security eFuses.

### 16 MB P4 build profile

The waterfall feature exceeds the previous 8 MB board's application slot. The
prototype received a 16 MB replacement flash on 25 November 2026. Active
`sdkconfig.defaults`, `partitions.csv` and the normal paired build still target
the 8 MB layout; they are not silently migrated. The guarded app updater now
accepts the separate 16 MB build only on an already matching installation. Use the separate profile below for the upgraded prototype.

A separate profile is provided by `firmware_p4/sdkconfig.16mb.defaults`
and `firmware_p4/partitions_16mb.csv`: two 6 MiB OTA slots, the existing NVS/OTA/PHY
prefix, 64 KiB coredump and the remaining 0x3D0000 bytes for assets. It must not be
used on an 8 MB chip. With ESP-IDF v6.1 activated (EIM shell or export.sh), run from
the **firmware_p4 directory** so all paths resolve consistently:

```sh
cd firmware_p4
IDF_COMPONENT_MANAGER=1 idf.py -B build \
  -D SDKCONFIG="$PWD/build/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.16mb.defaults' \
  -D HIGH_BOY_USB_VOICE=OFF reconfigure
cmake --build build --parallel 2
```

Use the standard `build` directory for this profile too. Existing sdkconfig values
take precedence over defaults; select the 16 MB flash size and partition filename
through `idf.py menuconfig` when switching an already configured 8 MB build. The ordinary 8 MB build's size check is
expected to reject this expanded application; no size check or CI budget is bypassed.
The guarded flash helper supports both app-only updates and full installation
of this 16 MB profile. Both require matching device capacity; only full mode takes a full 16 MiB backup. App-only mode requires the installed partition map to match. Full mode
uses the complete bundle from `build`, with the migration checks described below.
The separate recovery scripts retain their own reviewed profile restrictions.

Physical installation on 25 November 2026 identified P4 revision 3.2 and JEDEC
`0x182085` (16 MB). A complete 16 MiB backup confirmed the replacement was blank.
Bootloader `0x2000`, partitions `0x8000`, OTA data `0xF000`, application `0x20000`
and assets `0xC30000` were programmed with esptool 5.6.0 and verified by digest.
Watchdog reset started the application; the serial console and `c5 info` responded.
C5 and microSD were not flashed. Blank NVS starts the onboarding flow again.
These checks validate installation and basic boot/bridge operation, not RF
performance or long-term stability. Local backup and candidate hashes remain in
the ignored `integration/device_backups/` directory.

### ESP-IDF v6.1 compatibility

The normal builds and packaging checks now select v6.1 for both chips. Existing
SPI v2, partition maps, C5 memory policy and app-only update boundaries remain.
C5 `esp_hw_support`, `esp_libc` and `mbedtls` use `-Os`; driver and radio
compiler policies remain checked separately.
The UART line-editor patch is rebased to the v6.1 source and tested against the
actual SDK editor. C5 Muse is compiled for size to retain the installed OTA slots.

Active firmware crypto uses public PSA APIs for AES, SHA-256, HMAC, HKDF and
Meshtastic X25519 / Muse P-256 agreement. Shared checked operations live in
`common/crypto`, registered as `Service/firmware_crypto` in both firmwares.
Volatile PSA keys and operations are destroyed/aborted after use; wire formats
and existing persistent key encodings remain unchanged. Noise already selects
its PSA backend; its unused upstream Mbed TLS backend is not built.

The pinned v6.1 hardware AES PSA configuration advertises CCM but its AEAD
driver only accepts GCM. P4 therefore disables `CONFIG_MBEDTLS_HARDWARE_AES`
to use the supported software PSA implementation for Meshtastic CCM and NFC
CMAC. This also makes other P4 AES operations software-backed; runtime cost
requires device measurement. C5 retains hardware AES. Revisit the P4 setting
when an SDK driver supports the required algorithms, with crypto vectors and
hardware checks before re-enabling it.

NFC's DES/3DES implementation remains isolated from SDK configuration, with
upstream licenses and vectors preserved; see [NFC](../nfc/README.md).
`tools/crypto/test_psa.py --idf "$IDF_PATH"` builds the actual SDK software PSA
backend on the host and tests the production adapter and NFC crypto against
independent vectors, including authentication rejection and key cleanup.
The P4 CI job runs it separately from the SDK-free offline suite.

### Continuous integration and release boundary

Pull requests to `main`/`dev` and pushes to `dev` build the P4 and C5 in separate
version-pinned IDF containers. A separate job runs the offline source contracts,
Muse preservation, Sub-GHz, DOOM DMA and USB ownership tests. CI artifacts are
explicitly named `highboy-<target>-app-only`; they are build evidence, not a
verified full-flash or hardware-tested release.

Each target's build requires its generated `sdkconfig` after configuration and
again after compilation. The C5 check enforces all eight pinned target/memory
settings, rather than accepting the source defaults as build evidence. Offline
source-only jobs may run without build outputs; build jobs may not.

Commit validation runs on both pull requests and pushes to `dev`. It checks the
PR base-to-head range or the previous-to-current push range. A first push with a
zero previous SHA checks only the new tip, avoiding a retrospective check of
legacy history. Invalid or unavailable nonzero refs fail validation. Commitlint
uses exact package versions and the root `package-lock.json` through
`npm ci --ignore-scripts`; its accepted types match `AGENTS.md`.

The automatic `main` release runs semantic-release for versioning and changelog
publication. Its build job checks out the exact release tag, builds both chips
with ESP-IDF v6.1 and checks their internal-RAM/compiler policies. It uploads
explicitly named `app-only.bin` files, per-chip build evidence, SHA-256 checksums
and [application update instructions](APP_UPDATE.md). Build failure prevents
asset upload; the version/changelog release may already exist at that point.
These application assets do not include bootloaders, partitions or filesystem
images and do not authorize full restoration or production provisioning.

## Flash and storage boundary

The earlier V2.03 NFC expansion layout reallocated 256 KiB from P4 assets to the two
OTA slots, adding 128 KiB to each. C5 remains unchanged. Both P4 OTA slots and
rollback are retained; NVS, PHY and OTA metadata keep their offsets.

| P4 region | Previous local profile | NFC expansion profile |
| --- | --- | --- |
| `ota_0` offset / size | `0x20000` / `0x2D0000` | `0x20000` / `0x2F0000` |
| `ota_1` offset / size | `0x2F0000` / `0x2D0000` | `0x310000` / `0x2F0000` |
| `coredump` offset / size | `0x5C0000` / `0x10000` | `0x600000` / `0x10000` |
| `assets` offset / size | `0x5D0000` / `0x230000` | `0x610000` / `0x1F0000` |

The GPS expansion previously used two 0x300000-byte slots and assets at
0x630000 (0x1D0000 bytes). The current scanner expansion adds 64 KiB to **each**
P4 OTA slot, retaining equal slots and rollback. Lossless LZ4 compression of the
larger generated UI frames recovers space without removing assets. C5, NVS, PHY
and OTA metadata are unchanged.

| P4 region | Current offset | Current size |
| --- | --- | --- |
| `ota_0` | `0x20000` | `0x310000` (3,136 KiB) |
| `ota_1` | `0x330000` | `0x310000` (3,136 KiB) |
| `coredump` | `0x640000` | `0x10000` (64 KiB) |
| `assets` | `0x650000` | `0x1B0000` (1,728 KiB) |

The rebuilt LittleFS image is checked offline with all existing asset paths
retained. Allocation falls from 1,812 KiB to 1,660 KiB, including generated license
sidecars, leaving 68 KiB free in the smaller partition (previously 44 KiB).
Frame pixels are byte-identical after decompression. The table still ends at
0x800000 (8 MiB). Filesystem free space and OTA slot headroom are separate budgets.

This changes installation compatibility. The guarded app-only updater rejects the
previous tables (including 0x300000- and 0x2F0000-byte slots); it cannot migrate them. Package verification, recovery review and
the separate factory-security partition profile use the new geometry. Historic
packages/backups must be handled with their matching revision's tools.

Migration requires a backup and a serial flash of the new partition table,
initial OTA metadata selecting `ota_0`, application and rebuilt assets together.
Do not perform an app-only OTA or an assets-only flash to migrate. The old
`ota_1`, assets and coredump locations overlap the new layout and cannot be kept
as valid images. Clear the relocated coredump region during the reviewed migration;
its former contents belong to the old assets filesystem. Preserve any configuration stored in the old assets filesystem
before replacing it. NVS offsets and the SD card are unchanged; no erase-all or
eFuse changes are required by this layout. This source change performs no device
write. After migration, app-only updates again require an exact table match.

The first NFC-layout candidate (historical) measured 2,943,392 bytes in a 3,080,192-byte slot:
136,800 bytes free. The rebuilt LittleFS image is 2,031,616 bytes; mounting it
with littlefs-python reports 1,671,168 allocated bytes and 360,448 bytes free.
The 15 unused PNG sources and their generated rotated frames are absent.
These are build/image checks, not physical migration or NFC RF acceptance.

The last built baseline (`2b7614d3`) fit: P4 **2,943,520 / 2,949,120 bytes** (5,600 free),
C5 **2,016,096 / 2,097,152 bytes** (81,056 free). Layout/control screens use size
optimization; protocol/driver cores and emulators retain their previous flags.
These are historical baseline sizes, not size evidence for subsequent source
corrections. A fresh paired build and app-slot check are required before packaging.
There is very little expansion headroom, especially on the P4.

The post-import corrections were subsequently built with both pinned SDKs:
P4 **2,944,624 / 2,949,120 bytes** (4,496 free), C5
**2,016,048 / 2,097,152 bytes** (81,104 free). See
[the validation record](TESTS.md#corrected-firmware-build-record) for candidate
hashes and limits. These app-only builds do not authorize device restoration.

Do **not** use generic `idf.py flash`, generated `flash_args`, or an old hash-pinned
flashing wrapper for this candidate. The guarded app updater below checks the
actual P4, installed partition table, OTA selection and candidate hashes.

An earlier record reports a P4 erase for the standalone PS1 test; this is not
evidence of the currently connected device's state or recovery mode. An app-only
update cannot restore erased runtime assets/settings. Use the guarded full mode below for a blank device or supported partition migration.
Stock public assets remain references, not a verified recovery image.

### Updating an installed P4

Build the applications, bootloaders and assets with `bash tools/build.sh`. The launcher
first offers `ESP32-P4` / `ESP32-C5` / `Both chips`, then `Application only` / `Full flash`:

```sh
bash tools/flash.sh         # Select chip, then flash mode with Up/Down and Enter
bash tools/flash.sh p4      # Detect P4 port without the chip menu
bash tools/flash.sh c5      # Detect C5 port without the chip menu
bash tools/flash.sh both    # Update C5, then P4, using the same selected mode
```

To skip the mode menu:

```sh
./tools/flash.sh p4 --mode app
./tools/flash.sh p4 --mode full
./tools/flash.sh c5 --mode full -p /dev/your-c5-port --check
```

For a paired update, `both` selects the two interfaces of the same physical CP2105
instead of guessing from serial-port order. On macOS, pairing uses the physical
`IOUSBHostDevice` identity; duplicated VID/PID properties on serial drivers do not
create separate board identities. If pairing metadata is missing, use:

```sh
./tools/flash.sh both --mode app --p4-port /dev/your-p4-port --c5-port /dev/your-c5-port
./tools/flash.sh both --mode full
```

One mode selection applies to both chips. Each child uses its own SDK/Python
configuration and `firmware_<chip>/build`. Both build/partition/rollback artifacts
are checked offline before the first device connection. Identity, capacity and
security checks still run on each actual device. C5 runs first, followed by P4.
An error or cancellation stops the sequence and reports completed chips; this is
not an atomic two-chip update and does not undo a successfully updated first chip.
Use `--p4-port`/`--c5-port` for paired manual selection; a single `-p` is ambiguous
and rejected in `both` mode. Baud overrides and `--check` apply to both children.

C5 has the same inactive-slot OTA and full-backup policies as P4. For automatic
C5 download entry, the updater identifies the paired P4 console and requests
`c5 boot`, requiring the `C5 ROM boot: ESP_OK` acknowledgment before connecting
with no reset. A single C5 invocation can supply `--control-port P4_PORT` explicitly.
The console client answers ANSI status/cursor queries before sending the boot
command, including when linenoise was left in terminal mode by a serial monitor.
When the control console is unavailable or does not acknowledge, the updater
falls back to the normal esptool reset/connect sequence. `--before no-reset` skips
console entry for an already prepared loader. Both targets exit the loader through
esptool's chip-specific watchdog reset rather than relying on a cable reconnect.
After a C5 update, its paired P4 console is required: the updater runs `c5 wait 10`
and requires `C5 link: ALIVE` before completing or advancing to P4. This restores
SPI traffic within the C5 firmware's boot-health window; otherwise starting P4
flashing immediately can make the C5 roll back during P4's preflight reads.
Failure to restore the link stops the paired update.

Application mode preserves the current app, bootloader, partition table and assets;
it writes the inactive OTA slot and updates one selection sector only after verification. Full mode writes the generated bootloader, partition table, initial
OTA metadata, application to `ota_0`, and assets. It also clears the new coredump
region. Existing assets/settings stored in that filesystem are replaced, and OTA
selection resets to `ota_0`. It does not erase the entire chip or touch the other
processor or SD card.

Full mode validates the exact generated file list, offsets, capacity, bootloader
header and region bounds before connecting. It snapshots the selected binaries,
backs up the entire device, verifies that backup against the device immediately
before writing, and verifies every written image afterwards. Backup files and a
manifest remain under `integration/device_backups`. NVS and PHY partitions are
preserved: their offsets, sizes and types must match the installed table. A fully
blank chip is accepted; a nonblank chip with an unreadable table or a migration
moving NVS/PHY requires a separate recovery/data-migration procedure. `--check`
performs validation without writing; only full mode also creates a full backup.

Full-mode bundle/migration checks and write ordering have host tests; complete
P4/C5 build bundles were validated offline. This does not claim a physical test
of the new full-mode launcher.

All regular firmware builds and updates use one directory per chip:
`firmware_p4/build` and `firmware_c5/build`. Build, flash, packaging and CI consume
these same paths. The updater never searches alternate directories or chooses a
binary by modification time. Its compatibility `--build-dir` option only accepts
the corresponding standard directory. The 8/16 MB profile is determined from the
compiled configuration, not from the directory name. Switching profiles requires
reconfiguration of the same build. Device capacity must match; app mode also
requires matching installed partitions. The build script preserves its generated `build/sdkconfig`;
plain `idf.py build` reuses that configured build as well.

Generated project metadata, chip target, compiled configuration, flash settings
and the profile's source partition table must agree. C5 memory and paired-protocol
gates remain enabled. Offline tests cover full 16 MiB backups, capacity rejection,
app-only writes and rejection of alternate build directories.

Detection only reads USB descriptors; it does not open serial ports or reset the
other chip. High Boy V2.03 uses CP2105 (VID:PID 10c4:ea70): ECI/interface 0 is P4,
SCI/interface 1 is C5. macOS uses IORegistry interface ancestry with descendant properties (`ioreg -l`); Linux uses USB
interface location and Windows uses available interface metadata. Port names
and enumeration order are never treated as chip identity. The updater still
validates the selected ESP32 chip through esptool before writing.

Multiple matching devices bring up a port menu with USB serial/location. Missing
or conflicting metadata requires explicit `-p PORT`; generic USB-UART adapters
and native USB modes are not guessed. Non-interactive runs require an explicit
chip, `--mode app|full`, and either one identifiable interface or a manual port. Esc/Ctrl-C cancels
the menu without starting an update. These paths have offline metadata/PTY tests. On 2026-11-26, descriptor-only
detection on macOS identified the connected CP2105 P4 and C5 interfaces correctly.
This check did not open either serial port or start a flash update.

Manual selection remains available (Linux example: `/dev/ttyUSB1`; macOS below):

```sh
# Inspect OTA preflight only. This resets the device, but writes no flash bytes.
bash tools/flash.sh p4 -p /dev/cu.usbserial-01FDB4221 --check
# Validate, write the inactive app, verify it, then select it for the next boot.
bash tools/flash.sh p4 -p /dev/cu.usbserial-01FDB4221
```

Interactive terminals show a shaded purple ASCII Octobit above the current stage and
progress bar. A fixed light source, varied character density (`.:;+=xX$@`) and
256-color purple/gray shading give the mascot depth without animated noise.
The percentage describes the current backup/write stage, not the
whole operation; validation and reset show an indeterminate indicator entirely
inside the bar, without a trailing ellipsis. Completion is
shown only after the updater exits successfully. This presentation applies only
to `tools/flash.sh`, not `idf.py`, and does not change deployment guards.

Use `bash tools/flash.sh --preview` to inspect the artwork without an IDF
environment or device access. `--plain` retains the original output; redirected
output, CI, `TERM=dumb`, and `NO_COLOR` also select plain mode automatically.
The interactive view saves complete subprocess output in a private temporary
`highboy-flash-*.log` file and prints its path. Failures display the tool output
and preserve the exit status. Ctrl-C stops the updater and restores the terminal;
an interrupted write may still require recovery. These presentation paths are
covered by offline subprocess tests, not a new hardware-flash validation.

The updater uses the P4 Python/IDF paths from `.highboy-build.env`, explicit
`HIGHBOY_P4_IDF_PATH` / `HIGHBOY_P4_PYTHON_ENV`, or the active IDF environment.
Read and write speeds are independent: P4 defaults to **230400 read / 921600
write**, C5 to **230400 / 460800**. `--read-baud` and `--write-baud` override each
phase; `-b` overrides both unless a phase-specific option is supplied. Verification
keeps the current baud. The same stub and serial connection survive speed changes.

Application mode reads the partition table, OTA metadata and the current app
(exact image length, without unused slot padding) for checksum/SHA256 validation.
These preflight reads are temporary, not a full-flash backup. The installed
bootloader must match the build's header, load addresses and code/data (only
the IDF boot descriptor compile timestamp is ignored), with rollback enabled in
both generated configurations; otherwise use full mode to establish that contract.
An unconfirmed, corrupt, or exhausted OTA selection is rejected. No assets
filesystem is read or rewritten in application mode.

The new application is written to the inactive OTA slot and verified first.
Only then is a higher-sequence NEW entry written to the other OTA selection
sector. The old selection sector and old application remain intact for rollback.
If OTA metadata is entirely erased, the updater first establishes a VALID entry
for the checked current ota_0 image, verifies it, then selects ota_1 as NEW.
Each selection write affects one 4 KiB sector; it never erases both together.
The firmware's existing post-boot health check must confirm the new image;
otherwise the rollback-enabled bootloader rejects an unconfirmed image on a
subsequent reboot. This does not guarantee immediate recovery from every hang.
A failure before selection leaves the previous application selected.

Only full mode reads all flash in 512 KiB chunks and retains `flash.bin` under
`integration/device_backups/<chip>_<UTC>/`, with verification before writing.
Application mode retains the candidate, selection records and update manifest in
that directory, but no full snapshot. These private artifacts can contain device
data. Both modes enforce security, capacity and image/partition checks.

Both chips upload the RAM stub once. Per-command `--no-stub` prevents another
upload while reusing that stub. The final cleanup resets either chip by watchdog and closes the connection; each command has a 600-second timeout.

Hardware evidence on 2026-11-26: P4 stub reads failed at 460800/921600 but passed
at 230400 (512 KiB in 24.1 seconds); a complete 16 MiB backup was then read and
verified. An application write at 921600 completed in 32.6 seconds for 3,251,584
bytes, with digest and preserved-region checks followed by a working console.
An earlier interrupted write was caused by the user unplugging USB. That recovery
used the old active-slot workflow; it is not a physical rollback test of the new
inactive-slot workflow. Host tests cover selection, interrupted verification,
blank metadata initialization and speed transitions. A physical application
`--check` passed on the connected P4: valid current ota_0, inactive ota_1 at
0x620000, no full backup and no writes.

Paired application-flash validation on 2026-11-26 used automatic port discovery
and console boot entry for C5 then P4. C5 reads at 460800 reproduced serial
corruption; 230400 passed the complete preflight, including its 49,520-byte tail.
C5 wrote 2,156,064 bytes at 460800 in 33.1 seconds; P4 wrote 3,349,344 bytes
at 921600 in 34.4 seconds in the initial paired run. Both inactive images and
preserved active images passed digest verification. The initial run exposed a
C5 health timeout when P4 flashing started immediately. With the required SPI
confirmation added, the repeated paired run completed successfully. A subsequent
C5 reboot retained ota_1 at 0x320000, reported build ELF SHA256 `604c70aa0...`,
and returned `C5 link: ALIVE`. This validates application mode on this board,
not a new full-mode migration or an injected power-loss rollback test.

Before the default P4 connection, the launcher checks for a `highboy>` console
prompt at 115200 (up to 12 seconds to accommodate boot) and requests `p4 boot`, accepting it only after the firmware's
ROM-download acknowledgement. Otherwise it uses esptool's hardware-reset path.
Explicit `--before no-reset` skips this console probe. On 2026-11-26 the connected
P4 passed software boot entry, security/16 MB identification, a 4 KiB partition
read over one 460800-baud ROM session, and watchdog reset. No flash bytes were
written in that validation.

If initial ROM-baud connection fails, the script retries the selected baud without
reset (two attempts) to recover a loader left at high speed by an interrupted old
updater. It never performs a write as part of this recovery. When the running P4
console is available, issue `p4 boot`, close the serial console, then use
`tools/flash.sh p4 -p PORT --before no-reset`. The persistent connection supports
the requested baud in this mode too; separate 115200-baud reconnects are no longer
necessary. These changes do not bypass security, capacity or partition checks.

The reviewed `lab/rf` (`edc3182a`) updater also used `hard_reset`; the watchdog
fallback was in esptool for native P4 USB-OTG, not a lab firmware feature. It is
explicit here so USB-UART uses it too. Direct esptool invocations need
`--after watchdog-reset` explicitly; this script change does not alter `idf.py`
or a command that explicitly chooses `--after no-reset`. Release the physical
BOOT/LEFT button before resetting. No firmware update is required for this host
change. Host tests verify reset selection and sequencing. A physical P4 v3.2
USB-UART update on 2026-11-25 entered download mode with `p4 boot`, backed up
8 MB, verified the app write and returned to the firmware console using the
watchdog reset without a cable or battery disconnect. This does not validate
the firmware's RF behavior or other board revisions.
If serial communication itself fails, the watchdog command may not reach the
chip and it can remain in the ROM loader.

**C5 is not embedded or flashed by this integrated P4 build.** The dedicated
Quick Scan capture backend requires the matching C5 app. Progressive AP discovery, including ESP SCAN, requires matching
P4 and C5 applications with the channel-step commands (Wi-Fi 0x56–0x58). To update C5 separately:

```sh
# Close any IDF serial monitor using the C5 port first (Ctrl+]).
bash tools/flash.sh c5 -p /dev/cu.usbserial-01FDB4220 --check
bash tools/flash.sh c5 -p /dev/cu.usbserial-01FDB4220
```

The C5 target selects `TentacleOS_C5.bin` from its selected build directory, its own
partition table, 3 MiB app slots and the C5 SDK/Python environment. Paths come
from `HIGHBOY_C5_IDF_PATH` / `HIGHBOY_C5_PYTHON_ENV`, the local build environment
file or the active IDF environment. It explicitly selects `esp32c5`; P4 selects `esp32p4`. Both use the persistent
RAM stub, independently of the generated IDF flasher configuration. All other backup, security, image-integrity,
OTA-selection and data-preservation gates apply to both targets. C5 backups go
under `integration/device_backups/c5_<UTC>/`. The port can be explicit or detected from its CP2105 interface. The macOS C5
example is not a naming heuristic. A single-target update does not write the other
chip; C5 automatic download entry may use the P4 console as its controller.
The C5 updater passed host tests and validated the built C5 image; physical
backup/write acceptance remains pending.

### C5 application partition expansion

The C5 now uses the full 8 MiB address space with the following normal layout:

| Partition | Offset | Size |
| --- | --- | --- |
| `ota_0` | `0x20000` | `0x300000` (3 MiB) |
| `ota_1` | `0x320000` | `0x300000` (3 MiB) |
| `coredump` | `0x620000` | `0x10000` (64 KiB) |
| `assets` | `0x630000` | `0x1D0000` (1,856 KiB) |

NVS, OTA metadata and PHY offsets are unchanged. Each app slot gains 1 MiB by
using the previously unassigned 1,984 KiB tail and reducing assets by 64 KiB.
No asset files are removed. The factory-security review profile retains its
separate NVS/security offsets and uses the same expanded app/assets geometry.
Packaging, recovery validation and contract checks use the new normal table.

Existing C5 installations with 2 MiB slots require a migration, not an app-only
update. Back up flash and preserve filesystem configuration first; the new
partition table, initial OTA metadata selecting `ota_0`, app and rebuilt assets
must be installed together through a reviewed migration. Old `ota_1`, assets and
coredump ranges overlap the new map. The guarded updater rejects the old table;
this source change does not migrate or flash a device, or change P4 partitions.

Offline guard tests (SDK partition parser required):

```sh
python3 tools/highboy/test_flash.py "$IDF_PATH"
```

Serial and physical boot acceptance must be recorded separately from host tests.

## Validation limits

Successful compilation, symbol preservation and host tests do not prove physical
NFC/RF operation, electrical SPI reliability, SD/USB ownership, game frame rate or
cloud conversation. Muse currently provides text replies to voice notes; spoken
speaker replies are **not implemented**. USB voice remains disabled. Dormant
Flappy/Octopet/Motion screens remain excluded, exactly as in the prior local build.

Upstream HLE tools are retained, but the customized UI has not been certified by
that simulator. GPL and third-party notices remain applicable; source preparation
does not publish a release or claim endorsement by Meta/xAI.

## Firmware issue tracking

See the [issue audit](ISSUES.md) for completed implementation work, remaining
code/device/production requirements, and the latest paired build evidence.

## JavaScript and Lua runtime

The [Scripts service](../scripts/README.md) adds real MicroQuickJS and Lua
interpreters without changing the OTA partitions. This supersedes the baseline
optimization description above: Applications, Service and cJSON now use size
optimization and LTO; LVGL control/layout code follows that policy while draw
sources retain `-O2`. Driver and standalone emulator flags remain unchanged.
Device performance acceptance is still pending.

## P4 cryptography code size

The existing portable libsodium backend now compiles with `-Os`. Vendor sources,
algorithms, trust keys and hardware acceleration selections are unchanged; C5,
radio drivers, renderers and emulators retain their previous policies. In the
first linked comparison, this recovers 23,408 bytes relative to the same airtime-
guarded candidate, leaving 24,096 bytes in the P4 slot. This is code-size evidence,
not a claim of improved cryptographic throughput or side-channel certification.
The vendored libsodium 1.0.22 host suite was built separately with `CFLAGS=-Os`
and assembly disabled: all 101 tests passed, without skips. Device performance,
stack usage and timing acceptance remain pending under #157.

The current [peripheral interface matrix](MATRIX.md) distinguishes implemented
paths from missing adapters and physical acceptance. The [field guide](FIELD.md)
records the evidence needed for restoration, bench work and a later consumer
handoff; an app-only candidate cannot restore an erased P4.

## P4 portable crypto LTO checkpoint

The [2026-10-21 size comparison](SIZE.md) extends the earlier `-Os` policy with
P4 libsodium LTO while keeping `sodium/utils.c` outside LTO. On the current
ESP SCAN candidate this recovers 10,304 bytes, increasing the original app-slot
margin from 16 to 10,320 bytes. Vendor sources, partitions, keys and C5 policy
are unchanged. This supersedes the optimization policy above; runtime timing,
heap/stack and recovery-package acceptance remain open under #157.


## Verifiable app-only candidates

`python3 tools/highboy/package.py` rebuilds both pinned applications, runs the
complete offline suite and checks source/partition/memory contracts. It now
recognizes reviewed compiler-generated LTO symbol clones and the inlined Sub-GHz
console self-test (whose codec cases are covered by the host suite). It records
the actual linked evidence instead of requiring an optimized-away entry name.

The output is an immutable content-addressed directory under `integration/release`
with the P4/C5 application binaries and `manifest.json`. Files are copied into a
private staging directory, checked, then published by rename; failed copies or
changed source artifacts cannot leave a visible partial candidate. Repeated
identical packaging preserves the existing directory. The manifest records image
sizes/hashes/margins, SDK lanes, protocol 2, partition hashes, source revision and
dirty state, compile-command fingerprints and the actual test runner fingerprint.
App-signing mode and the selected public trust-header fingerprint are recorded;
key custody and hardware acceptance remain unverified.

Verify a received directory with Python's standard library only:

```sh
python3 tools/highboy/verify_package.py integration/release/<candidate-directory>
```

Verification checks both images, exact profile, slots and SHA-256 values. It
rejects mismatched SDK/protocol/partition settings, missing/corrupt/swapped images,
symlinked files and fabricated runtime/provisioning acceptance. This is an
integrity/profile check, not signature authentication or a production release.
No bootloader, asset image, private credential or production key is included.

For an already running matching integrated installation, the P4 SD update path is
`/sdcard/update/tentacleos.bin`, selected from the candidate's P4 application.
The native P4 Update screen now checks the real file, displays the installed
runtime version and runs actual OTA on OK; its progress and error come from the
writer. It updates P4 only. C5 uses its independent existing update path. A device
with another partition map, missing assets or an erased boot/app still requires
an approved restoration/migration package. Package verification alone does not
make those states eligible for either update path. See [OTA](../ota/README.md).


## Private recovery review bundles

`tools/highboy/recovery.py` prepares and verifies a private paired review bundle
from two explicit, complete guarded-updater backups and a verified app-only
candidate. It checks capture hashes, both exact partition maps, boot/application
image checksums/digests/chip IDs, OTA selection, slot sizes and asset presence
before atomically publishing anything. The bundle retains full snapshots and
per-chip candidate offsets; no device or flash action is implemented.

This supplies recovery preparation for #157/#158, not a deployable public release.
Full snapshots can contain per-device credentials/identities; bundles are private
and must not be committed or uploaded. Inside this checkout, output is restricted
to ignored `integration/recovery_candidates/`. Asset presence does not establish
filesystem health, and physical identity/security/paired boot/full-restore review
remain explicit false gates. See [the recovery procedure](RECOVERY.md).
