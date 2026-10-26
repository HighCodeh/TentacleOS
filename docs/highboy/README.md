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
source hashes, upstream hashes and relocated paths; it is provenance, not a claim
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
| Apps / games / power | Preserve glass-style Apps menu, covers, Game Boy controls, DOOM, Fly/Musca, brightness and display-hold behavior. |

Upstream's C5 tree is identical between the previous upstream snapshot and this
revision. Replacing our C5 with the stock image would remove Muse and regress the
local protocol. Both integrated targets retain identical SPI version **8** headers;
upstream currently has version 6. The Muse extension remains a separate category.

## Build

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
Build products live in `firmware_{p4,c5}/build_highboy`, not the previous build
directories.

P4 loads only `firmware_p4/sdkconfig.defaults`, including its installed partition
offset and development-security settings. The former `sdkconfig.muse` overlay
has been merged into that file. C5 still loads `sdkconfig.defaults` followed by
`sdkconfig.baseline` to preserve its memory policy. Factory-security review
profiles remain separate from these normal development defaults.

The P4 app-signing trust store can be selected with `TOS_APP_TRUST_PUBLIC_KEY`
(64 public-key hex characters). `TOS_APP_PRODUCTION=ON` requires an explicit key
and rejects the shipped development identity. This does not enable Secure Boot,
Flash Encryption or provision eFuses. See [SDK trust configuration](../app_sdk/README.md#keys--trust-store).

Build only produces applications. It does not access serial ports, flash, SD,
microphone, token storage or any live Muse API. It does not regenerate runtime
assets, embed a different C5 recovery image or migrate security eFuses.

### ESP-IDF v6.1 compatibility

The normal builds and packaging checks now select v6.1 for both chips. Existing
SPI v8, partition maps, C5 memory policy and app-only update boundaries remain.
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

The legacy automatic `main` release is deliberately stopped before
semantic-release when this integrated profile is present. That pipeline assumes
one SDK, older output paths and a complete deployable firmware. It must not
publish these app-only candidates under those assumptions. Enabling a public
release requires a separately reviewed paired deployment and asset/partition
migration package.

## Flash and storage boundary

The previously used **local High Boy map** is retained: P4 app slots are
`0x2D0000` bytes; C5 app slots are 2 MiB. This is not the current upstream P4 map:

| P4 region | Upstream `011ed7d` | Integrated local profile |
| --- | --- | --- |
| `ota_0` offset / size | `0x20000` / `0x2E0000` | `0x20000` / `0x2D0000` |
| `ota_1` offset / size | `0x300000` / `0x2E0000` | `0x2F0000` / `0x2D0000` |
| `coredump` offset | `0x5E0000` | `0x5C0000` |
| `assets` offset / size | `0x5F0000` / `0x210000` | `0x5D0000` / `0x230000` |

NVS, OTA metadata and PHY offsets are the same, but an upstream-installed device
still needs an explicit migration review. Do not apply the local profile's
offsets or assets image to it blindly. No partition is enlarged merely to pass
the app size check. The previous local P4 image had only 1,840 bytes of margin,
so the final linked size is an explicit release gate.

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
update cannot restore erased runtime assets/settings. A separately reviewed full
restore package is still required for an erased device or partition migration.
Stock public assets remain references, not a verified recovery image.

### Updating an installed P4

Build the current applications with `bash tools/build.sh`. Select the P4 serial
port explicitly (Linux example: `/dev/ttyUSB1`; macOS example below):

```sh
# Backup and inspect only. This resets the device, but writes no flash bytes.
bash tools/flash.sh p4 -p /dev/cu.usbserial-01FDB4221 --check
# Backup, validate, update the selected app partition and verify its bytes.
bash tools/flash.sh p4 -p /dev/cu.usbserial-01FDB4221
```

The updater uses the P4 Python/IDF paths from `.highboy-build.env`, explicit
`HIGHBOY_P4_IDF_PATH` / `HIGHBOY_P4_PYTHON_ENV`, or the active IDF environment.
`-b 115200` selects a slower serial baud rate if needed. It reads security flags,
rejects enabled Secure Boot/Flash Encryption, and backs up the entire flash in
`integration/device_backups/p4_<UTC>/flash.bin` before any write. These ignored,
private directories can contain device settings; keep the backup locally.
The reviewed profile requires an 8 MB flash device. Backup reads use 512 KiB
blocks with progress and a 120-second limit per serial command; a failed or
partial backup cannot reach the write step.

The installed partition table must exactly match `build_highboy`, including the
preserved local offsets. The existing bootloader and OTA-selected app must have
valid P4 image checksums/SHA256; ambiguous or pending OTA state is rejected.
An entirely blank assets partition is rejected. This is only a presence check,
not proof of a healthy filesystem or complete assets. A successful update writes
only the selected app partition, keeping OTA selection, bootloader, partitions,
assets, NVS and SD unchanged. The adjacent manifest records backup/candidate
hashes, chosen slot and write/verification status. Interrupted in-place updates
can require recovery from backup; this is not an automatic rollback mechanism.
If a check fails, no migration or full restoration is attempted. A failed serial
operation can leave the device in the ROM loader; reset it to resume normal boot.

**C5 is not embedded or flashed by this integrated P4 build.** The dedicated
Quick Scan capture backend requires the matching C5 app. ESP SCAN uses the
existing AP-scan commands and only needs a P4 update when C5 already supports
those commands. To update C5 separately:

```sh
# Close any IDF serial monitor using the C5 port first (Ctrl+]).
bash tools/flash.sh c5 -p /dev/cu.usbserial-01FDB4220 --check
bash tools/flash.sh c5 -p /dev/cu.usbserial-01FDB4220
```

The C5 target selects `firmware_c5/build_highboy/TentacleOS_C5.bin`, its own
partition table, 2 MiB app slots and the C5 SDK/Python environment. Paths come
from `HIGHBOY_C5_IDF_PATH` / `HIGHBOY_C5_PYTHON_ENV`, the local build environment
file or the active IDF environment. It explicitly selects `esp32c5` and uses
`--no-stub`, matching this profile's C5 flasher configuration. P4 uses
`esp32p4` and its existing stub path. All other backup, security, image-integrity,
OTA-selection and data-preservation gates apply to both targets. C5 backups go
under `integration/device_backups/c5_<UTC>/`. The port is always explicit; the
macOS C5 example is the port previously used by this environment's C5 monitor,
not an automatic selection rule. A C5 update does not touch P4, and vice versa.
The C5 updater passed host tests and validated the built C5 image; physical
backup/write acceptance remains pending.

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
sizes/hashes/margins, SDK lanes, protocol 8, partition hashes, source revision and
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
