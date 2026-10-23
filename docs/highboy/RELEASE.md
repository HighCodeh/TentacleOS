<!--
SPDX-License-Identifier: GPL-3.0-or-later

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

# Performance, internal RAM and paired OTA candidates

Issue #157 is completed at the requested software boundary. The existing
[size policies and comparisons](SIZE.md), current linked-memory gates and paired
SD OTA packaging supply development candidate evidence. They do not designate a
production release, change the version, enable security provisioning or certify
runtime performance. Versions remain controlled by the existing release workflow.

## Reproducible build evidence

The normal P4 v5.5.3 and C5 v6.0.1 builds retain protocol 8, their original app
slots and the C5 memory overlay. `package.py` rebuilds both applications, runs the
complete offline suite and adds a `build_evidence` record to each target in the
candidate manifest. The record binds the application, ELF, map, sdkconfig,
partition binary and compilation database by SHA-256. Changing an artifact
during collection fails the audit. The SDK checkout must match its pinned tag.

Each build's configured SDK Python runs its own `esp_idf_size`: the P4 lane uses
the legacy JSON schema, while C5 uses JSON2 1.2. Only main internal RAM is counted:
P4 DIRAM or C5 HP SRAM. Flash, external RAM and C5 LP SRAM are not summed into
internal heap. Static usage includes linked code/data, not just BSS.

The reviewed minimum **linker** headroom is 160 KiB on P4 and 60 KiB on C5.
These regression floors retain most of the current headroom; they are not
guarantees that runtime allocations will fit. The C5's 64 KiB internal malloc
reserve is a runtime allocation policy, not another quantity to add to linker
headroom. Both original application slot limits also remain enforced.

The audit checks the effective final optimization and LTO flags for P4 CC1101,
audio, SPI bridge PHY, DOOM, Game Boy, LVGL software blending, app control and
the deliberately opaque sodium utilities. C5 SPI slave, Muse bridge and Wi-Fi
retain their existing `-Og` policy. Compiler flags are examined in order so a
later override cannot silently defeat the reviewed policy. This is a selected
critical-path guard, not a claim that every compiled object was benchmarked.

To audit a single completed build without rebuilding or accessing a device:

```sh
python3 tools/highboy/build_evidence.py --target p4 \
  --build firmware_p4/build_highboy --out /tmp/p4-build-evidence.json
python3 tools/highboy/build_evidence.py --target c5 \
  --build firmware_c5/build_highboy --out /tmp/c5-build-evidence.json
```

Output paths must be new. CI collects the same per-target report after the
normal build and includes it with the app-only build artifacts. The offline
release regressions use explicit memory/compiler/image fixtures, with failures
for memory regressions, policy overrides, mismatched artifact hashes, corruption,
symlinks and incomplete publication. They do not simulate real timing results.

## Paired SD package

```sh
python3 tools/highboy/package.py
python3 tools/highboy/ota_package.py prepare \
  --candidate integration/release/<candidate-directory> --out integration/release
python3 tools/highboy/ota_package.py verify integration/release/<ota-directory>
```

The new immutable, content-addressed directory contains the verified source
candidate, `ota.json`, `INSTALL.txt` and:

```text
sdcard/update/tentacleos.bin
sdcard/c5/TentacleOS_C5.bin
```

Copy the contents of `sdcard/` to the SD root for an already running, matching
integrated installation. These are the existing independent
[P4 and C5 OTA inputs](../ota/README.md). Neither updater updates the other chip;
keep both protocol-compatible images together. Existing boot/partition/assets
and backup requirements still apply. P4 confirms local assets/render health;
C5 confirms P4 bridge activity. Interrupted updates and actual rollback behavior
still need device acceptance.

The exporter requires current linked build evidence for both chips. Old
app-only candidates remain readable with `verify_package.py` but must be rebuilt
and repackaged before SD export. Staging is checked before atomic publication;
failed copies leave no visible package. Existing corrupted outputs are rejected,
never overwritten. Verification binds the candidate manifest and checks both SD
copies, exact paths, installation instructions and the absence of extra payloads
or symlinks. This is integrity/profile verification, not signature authentication.

There is no bootloader, assets image, private snapshot, key or eFuse operation
in this OTA package. It cannot restore an erased device or install the separate
factory-security partition map. Preparing it never opens a serial port or SD
device and never writes firmware.

## CI host and container requirements

The Linux host matrix keeps both GCC and Clang with warnings treated as errors
and ASan/UBSan enabled. The radio-view harness explicitly requests POSIX.1-2008
declarations while retaining C11, since `strnlen` is not an ISO C11 function.
OTA fault fixtures use explicit conditional blocks accepted by GCC's
`-Wmisleading-indentation`; the warning remains enabled.

The IDF container job explicitly adds only `GITHUB_WORKSPACE` to Git's global
`safe.directory` list and verifies `HEAD` before building. This is needed when
the mounted checkout and container process have different ownership. It avoids
the `dubious ownership` failure when recording factory-build provenance after
compilation. Other repositories are not trusted by this step; it uses no wildcard.

Run host regressions from a clean checkout of the published revision when
diagnosing Actions, so pending local firmware changes cannot hide a failure.
A passing local macOS/Clang suite and GCC syntax checks do not certify the full
Linux matrix; final Actions acceptance requires a run containing the fixes.

## Acceptance outside software closure

No hardware was accessed for this work. Runtime latency, radio throughput,
internal heap minima/largest blocks, stack watermarks, repeated start/stop and
paired OTA/rollback require observed results on a matching device. Record them
with the existing `free`, `stack`, `tasks` and companion diagnostics following
the [field guide](FIELD.md); linker headroom is not a free-heap reading.

Production identity/key custody, approved full restoration/assets or metadata
migration and physical secure boot/recovery remain release prerequisites in
the [security procedure](../security/README.md) and
[private recovery review](RECOVERY.md). Those gates remain explicitly false in
software artifacts after closing #157; issue closure does not grant deployment
approval or assert that a consumer release is ready.
