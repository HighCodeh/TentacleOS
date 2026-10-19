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
that subsequent edits are identical to the import.

- User interfaces and emulators: `firmware_p4/components/Applications`.
- Hardware drivers: each target's `components/Drivers`.
- Muse controller, wire protocol and SDK adapters: `components/Service/highboy_muse`.
- P4 Muse board integration: `components/Service/muse`.
- C5 Noise and MP3 dependencies: `components/Service/noise_core` and `minimp3`.
- Resource arbitration: `firmware_p4/components/Core/resource_mgr`.
- exFAT override: `firmware_p4/components/Service/fatfs`.
- Offline, pinned third-party dependencies: each target's `vendor` directory.

Only the four upstream architecture roots are used under `components`.
Independent nested components are registered explicitly through CMake.

## Merge decisions

| Area | Decision |
| --- | --- |
| DOOM | Upstream internal-DMA strips, retaining local input hygiene and LVGL lock order. |
| USB microSD | New TinyUSB storage-handle API and no automatic formatting; safe handling of deferred writes during teardown. |
| Sub-GHz | Restore upstream protocol encoders, decorated-name lookup, serialization and offline self-test; retain local RF hardware configuration. |
| Dependencies | Apply upstream LVGL `<9.6` constraint; retain the reviewed offline vendors and DES support. |
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

The integrated build intentionally has two toolchain lanes: P4 uses ESP-IDF
**v5.5.3**, while the existing Muse-enabled C5 requires **v6.0.1**. Do not build the
C5 with the generic upstream 5.5.3 container and assume it is equivalent.
For a single target, activate the matching installed IDF and run its build. The
script checks that the IDF checkout is exactly the lane's release tag. For a
two-target build, set `HIGHBOY_P4_IDF_PATH` and `HIGHBOY_C5_IDF_PATH` to their
respective installed checkouts. Optional `HIGHBOY_P4_TOOLS_PATH` /
`HIGHBOY_C5_TOOLS_PATH` and `HIGHBOY_P4_PYTHON_ENV` / `HIGHBOY_C5_PYTHON_ENV`
select separate installed tools and Python environments; otherwise IDF uses its
active/default environment. No personal filesystem paths are built into the
script.

These variables may be placed in the ignored repository file
`.highboy-build.env` (or a file selected by `HIGHBOY_BUILD_ENV`). This file is
sourced as local shell configuration: use a trusted file containing toolchain
paths only, never account credentials. Explicit exported `HIGHBOY_*` toolchain
variables take precedence over that file. Install IDF/tools and initialize their
required submodules before building. Firmware dependencies are vendored,
component-manager resolution is disabled, and the build does not install tools.
Build products live in `firmware_{p4,c5}/build_highboy`, not the previous build
directories.

The P4 app-signing trust store can be selected with `TOS_APP_TRUST_PUBLIC_KEY`
(64 public-key hex characters). `TOS_APP_PRODUCTION=ON` requires an explicit key
and rejects the shipped development identity. This does not enable Secure Boot,
Flash Encryption or provision eFuses. See [SDK trust configuration](../app_sdk/README.md#keys--trust-store).

Build only produces applications. It does not access serial ports, flash, SD,
microphone, token storage or any live Muse API. It does not regenerate runtime
assets, embed a different C5 recovery image or migrate security eFuses.

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
flashing wrapper for this candidate. `tools/flash.sh` deliberately stops before
device access. A deployment needs the actual target identities, installed
partition table, OTA selection and approved candidate hashes.

The current physical P4 was previously erased for the standalone PS1 test.
An app-only return cannot restore erased runtime assets/settings. A separately
reviewed restore package is required; this source update does not pretend those
assets are present. The C5 and microSD are not touched here. Stock public assets
remain in the repository as references, not a verified recovery image.

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
