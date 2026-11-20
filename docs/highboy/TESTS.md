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

# Offline preservation and Muse regressions

The historical import manifests under `integration/` are optional local files,
not repository inputs. Without all three manifests, a fresh clone skips the four
import-history checks explicitly; protocol, DMA, license, memory, fixture and
functional host checks still run. If only part of the local audit exists, the
suite fails instead of accepting incomplete provenance. Existing historical
paths and reports below describe the original local review artifacts.


## P4 app updater validation (2026-10-20)

`test_flash.py` passed 16 offline cases covering OTA selection, mismatched maps,
blank assets, truncated flash, security refusal, backup-only behavior, selected
slot writes, backup timeout/short reads and reset attempts after failed checks.
The existing 34 preservation tests also passed; shell syntax and diff whitespace
checks passed. No firmware sources changed for this updater.

Read-only serial checks identified the P4 on `/dev/cu.usbserial-01FDB4221` as
ESP32-P4 revision v1.3 with 8 MB flash, Secure Boot disabled and Flash Encryption
disabled. The installed partition payload matched the built local table
(`bb76257d010de4925ddf8925b46a2f89e0512a2a858e0c4939d705c74a1e51bd`).
An initial monolithic backup was interrupted after more than 12 minutes at 21%;
no complete backup was produced. A subsequent 4 KiB table read completed and
issued a hard reset. The updater now uses bounded 512 KiB reads. Reconnection
for that full check failed with `No serial data received`; chunked backup,
existing app/boot/assets validation, app writing and physical boot acceptance
remain unverified. No flash bytes were written and C5 was not accessed.

## Running the offline suites

The ESP SCAN screen was subsequently routed through the existing normal C5 AP
scan, with P4-local OUI/RSSI classification and no monitor-mode or radio
STOP/START commands. The existing Espressif prefixes are compiled as a compact
P4 lookup. Six sanitizer groups, 34 preservation tests and contract checks passed.
The P4 build on IDF 5.5.3 fits at 2,948,880 / 2,949,120 bytes (240 free).
This route requires only the P4 update when C5's normal AP scan is available;
no device access or physical scan was performed for the routing change.

The updater was subsequently extended to select P4 or C5 explicitly. All 20
offline deployment tests passed, covering C5's 2 MiB slot map, C5 backup-only
mode, selected second-slot writes and explicit `esp32c5`/`--no-stub` arguments,
with the existing P4 guards retained. The actual built C5 image also passed
checksum/SHA256 validation under the C5 Python environment. Neither chip was
accessed or flashed while adding C5 support; physical C5 acceptance is pending.

Run from the canonical repository root:

```sh
python3 tools/highboy/test_preservation.py
python3 tools/highboy/test_subghz.py
python3 tools/highboy/check_contracts.py
```

`test_preservation.py` does not build either firmware or access hardware,
serial ports, network services, real account credentials, or a microphone.
Its host binaries and copied test fixtures exist only in temporary directories.
It reads public source and the import manifest; generated configuration is
checked only for the named target and memory-policy settings, never printed.

## Preservation boundary

The import gate checks all entries in `integration/import.json` against the
original local SHA-256. Entries marked `upstream_equivalent` may differ only in
CRLF versus LF line endings. Their normalized bytes are checked against
`normalized_local_sha256`, generated once from each original source only after
verifying its original `local_sha256`. All 15,477 original hashes and provenance
fields remain intact. No original workspace is read during the tests. Missing
files/hashes, duplicate targets, and unexpected content changes fail the gate.

An explicit, file-by-file `REVIEWED_MERGES` list excludes the intentional project
build/configuration changes, upstream DOOM/USB-MSC merge, restored upstream
reserved radio API declarations, and reviewed SubGHz repairs from the
byte-preservation assertion. The P4 Muse reply-text bounds correction also has
an explicit reviewed source seal, review base and reason; its superseded
formatting proof remains archived in the import record. Those exclusions are reported, not
described as unchanged files. Their functional validation belongs to the SubGHz
suite and firmware build/review. Every changed exception must have its sealed
`reviewed_target_sha256`; the gate pins that post-merge output as well. A revert
to the original source does not bypass an existing reviewed-output seal.
The P4 Applications CMake exclusion includes the explicit size-optimization
policy for selected UI/theme/Home/Menu/ConnectWiFi/NFC-screen translation units;
it does not exempt any of those production C source files from preservation.

Automatic first-party formatting is recorded in `integration/formatting.json`.
Every published record pins the exact input and output SHA-256 and formatter.
For an imported file, the input must match its reviewed-output seal if present,
otherwise its original local hash. An `upstream_equivalent` input may instead
match its separately pinned upstream hash. A normalized hash alone does not
authorize a formatter input. The current file must match the recorded output;
formatting never discards or rewrites the original import hashes. These chains
are explicit provenance, not an assertion that a formatter proves semantics.
The tests read only the published manifest, never a private Git hook journal.

Critical Muse runtime, pairing, setup, P4 PTT app, SPI bridge, DMA driver, Wi-Fi
guards and BLE service files have the same exact-source-or-sealed-format-chain
checks in addition to the inventory gate. Other checks
cover matching SPI v8 headers and local extensions, static DMA transaction
lifetime, the absence of automatic NVS erasure in existing initialization paths,
the C5 memory policy, and SDK/license provenance metadata. Source markers are
preservation guards, not proofs of runtime behavior or hardware timing.

The C5 configuration check reads the unified `sdkconfig.defaults`, then verifies the effective
`firmware_c5/build_highboy/sdkconfig` when present. A missing generated
configuration is reported as pending, never as a successful build validation.
The build wrapper separately invokes `check_contracts.py --require-built <target>`
after configuration and after compilation, requiring the generated configuration
and enforcing the shared `C5_MEMORY` policy in the C5 build job itself.
`test_contracts.py` covers missing outputs, missing/incorrect/disabled C5 settings,
defaults overrides, valid configurations and prohibited security changes using
temporary files. It never edits the actual generated build configuration.

Commit validation regressions run in their CI job after `npm ci --ignore-scripts`:

```sh
node --test tools/ci/test_commitlint.mjs
```

They exercise accepted/rejected types, breaking-change headers, the 200-character
limit, invalid intermediate commits in a range, missing refs and first-push
handling in isolated temporary Git repositories with hooks disabled.
The updated build wrapper passed incremental C5/P4 builds with the required
configuration checks. All eight configuration-gate tests, both commit-validation
test groups and the complete offline regression runner passed locally. These
workflow changes still require a committed push or PR to execute on GitHub Actions.

## Canonical-source host tests

The runner compiles the actual relocated production sources under
`firmware_c5/components/Service/muse_port` and
`firmware_p4/components/Service/muse_port`, using strict C11 warnings and
UndefinedBehaviorSanitizer. Exactly 15 established public fixture/fake files
are included under `tools/highboy/fixtures/muse_port`; no legacy production
source, entire legacy directory, runtime asset, or account data is copied.
`PROVENANCE.json` records each original source path, byte count and SHA-256.
These original fields are retained for all 15 fixtures. Only `p4_app/test_app.c`
and `pairing/test_setup.c` have separately sealed `reviewed_sha256`,
`reviewed_bytes` and `review_reason` fields: bounded-text regressions and GCC
warning cleanup. The verifier requires complete valid review fields for exactly
those whitelisted paths and rejects reviews on other fixtures. The fixture
inventory and current original-or-reviewed hashes are checked before compilation.
Fixtures are then copied to a temporary directory so quoted includes resolve
to canonical headers rather than adjacent legacy production headers. Pairing
tests use managed cJSON at the version declared by both firmware manifests.

| Host suite | Coverage |
|---|---|
| Core | 32 PTT/control scenarios |
| Wire codec/server | 25 framing, bounds and sequencing groups |
| Wire client | 21 deterministic retry/state groups |
| PCM adapter | 21 audio-boundary groups |
| Endpoint | 17 adapter/endpoint groups |
| P4 app | 20 application/PTT groups, including five malformed-text rejection cases |
| Pairing adapter | 60 isolated scenarios; cryptography/store boundaries faked |
| Passive GATT/fragment codec | 11 groups; NimBLE explicitly faked |
| Setup owner | 27 isolated scenarios; SDK/GATT/runtime/store boundaries faked |
| RPC | Replay, lost/empty responses, sequence, fence and capacity assertions |
| Wi-Fi JSON pre-parser | 17 bounds/depth/NUL cases |

These tests exercise canonical source, not a previously built binary. They do
not validate real BLE pairing, cryptographic handshakes, cloud authentication,
electrical SPI/IRQ timing, DMA under hardware load, or microphone/speaker audio.
The full runtime worker and SDK network implementation are not executed by this
host selection. Exact import hashes protect their existing content, but do not
substitute for target builds and subsequent explicitly authorized device tests.

## Offline dependencies and options

A host `cc` compiler with UBSan and Python 3 are required. `CC` selects the host
compiler (shell-style arguments are parsed with `shlex.split`, not executed by
a shell), so GCC and Clang CI jobs compile the same canonical sources and strict
warning/UBSan flags. All source, fixture,
normalization and license references are contained in this repository; the
tests do not depend on a sibling workspace or an absolute user-directory path.
Prepare the managed cJSON source before the first run (or after a version change):

```sh
python3 -m pip install -r tools/dependencies/requirements.txt
python3 tools/dependencies/prepare_host.py
```

Preparation uses network access as needed; the tests themselves stay offline.
The preparation step resolves only cJSON with IDF version metadata, without
installing or compiling an SDK. `--skip-host` explicitly runs only the
source/configuration gates; skipped host tests are visible in the test summary.

License checks establish that the preserved license/provenance files exist and
the SDK Apache license text matches the hash verified against its original.
They are inventory checks, not a legal opinion or an automatic distribution
compliance certification.

## Post-import source corrections

`integration/corrections.json` records each explicit source correction against
base commit `2b7614d3ce6bf1a2cad8e34ed5d27fd849208ed4`, including its input hash,
output hash and reason. The original import manifest and formatting records
remain unchanged. The gate requires each input to match its original, reviewed
or formatted seal, and rejects changed outputs, unknown inputs and incomplete
proofs. A correction cannot bypass a prior reviewed-source seal. These records
are source evidence, not proof of a target build or hardware acceptance.
When the automatic pre-commit hook formats a correction, `pre_format_sha256`
retains the reviewed source hash and `formatter` identifies the tool; the output
seal remains the exact hash of the formatted file. Original import and earlier
formatting evidence is retained.

The offline runner also executes `test_rf_ownership.py`. It compiles the actual
RF frontend plus the production NFC HAL write/field/deinit functions and RFID
reader lifecycle functions with explicit hardware and scheduler fakes. Host
mutexes exercise 200 simultaneous LF/HF claim rounds. Failure injection covers
OFF errors, uncertain startup transfers, oscillator/gain rollback errors and
retained transport on failed deinit. Reader cases cover coherent results,
cancellation before/during a read, reopening while busy and task creation failure.
No RF field, GPIO, account or serial device is accessed.

The preexisting canonical Muse suites cover the portable app and adapters, not
the complete native P4/C5 worker implementation. Firmware acceptance requires
fresh P4/C5 builds, app-slot checks and physical device tests. HLE/emulator
validation is outside the current firmware review scope. Baseline binary sizes
do not certify these corrected sources.

`test_muse_native.py` also compiles the complete native P4 Muse adapter with its
real portable controller/codec and explicit device-interface fakes. It checks
RPC payload bounds and command preservation, recording-buffer allocation errors,
recorder-task rollback, owner-allocation fallback and control-task failure. It
does not start a task, access NVS, record audio or contact the C5.

## Corrected firmware build record

The working-tree corrections based on `2b7614d3` passed both application builds
in C5-then-P4 order, with component-manager downloads disabled, USB voice disabled
and the original configuration overlays and partition tables retained.

| Target | Exact ESP-IDF commit / tag | App bytes | Slot bytes | Free bytes |
|---|---|---:|---:|---:|
| C5 | `8c19b156084a0753687347cca1f5355782893533` / `v6.0.1` | 2,016,048 | 2,097,152 | 81,104 |
| P4 | `2c211b236707889e8400c4dc5644dd5c4ee071e0` / `v5.5.3` | 2,944,624 | 2,949,120 | 4,496 |

Candidate SHA-256 values for `firmware_<target>/build_highboy/TentacleOS_<TARGET>.bin`:

- C5: `8d881e9568af9308f7efaa6c866fcd414d51a1f2e7b92c048eeb1840272f73a1`.
- P4: `4c5752823ee424363209f4817275e9c63a14176512a3c8ecb69b83869fa4fd92`.

SDKs and their required submodules were prepared separately before the offline
builds. The P4 used `IDF_SKIP_CHECK_SUBMODULES=1` after initialization of its
required pinned submodules; unused radio/other-chip submodules were omitted.
The C5 build wrapper checks its thirteen required submodules before setting the
same flag. Neither SDK's tracked source was patched. The generated C5
configuration retains the 65,536-byte internal reserve, external NimBLE allocation
and external task-stack permission.

Both application-size checks and the paired SPI v8/source-contract checks passed.
`bash tools/highboy/test.command` then passed all 34 preservation/portable-host
tests without skips, RF ownership and native Muse regressions, Sub-GHz checks
and all three DOOM/USB tests. The C5 memory check used the generated configuration.
Builds still emit existing unused-symbol, truncation and deprecated-API warnings;
the P4 SX1262 HAL also warns about disabled negative GPIO constants inside
constant-false branches. Successful compilation does not certify a warning-free
repository. Formatting remains delegated to the commit hook and CI, as required
by `AGENTS.md`. No hardware, restore, flashing or live Muse session was exercised.
HLE is outside this firmware review scope.

## Script runtime build record

The JavaScript/Lua candidate was validated on 2026-10-19 after firmware source
commit `cfc01f51`. P4 was built with the pinned ESP-IDF v5.5.3 toolchain; the C5
source is unchanged and passed the paired ESP-IDF v6.0.1 build. Both generated
configurations and the shared SPI v8 contract passed the offline checks.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,943,072 | 2,949,120 | 6,048 | `6462fe37f90733ef03d26ee5bb2b2311adcfb78e4ad370baccfb7af7c962befa` |
| C5 | 2,016,048 | 2,097,152 | 81,104 | `4b803dcd2c9dc34b3258663e1d86cec807b6ff92595146a3a53677eb1b8c2b1d` |

`bash tools/highboy/test.command` passed, including generated JavaScript ROM
verification, pinned interpreter source hashes, real JavaScript/Lua ASan/UBSan
execution, 199 Lua allocation-failure positions, memory churn, exceptions,
protected-loop cancellation, SD worker ownership, task-creation failure, deadline
and transcript bounds. The existing configuration, RF/NFC/RFID, IR, Muse,
Sub-GHz, preservation, DOOM DMA and USB MSC checks also passed. Commitlint passed
for all interpreter changes; commit messages contain only titles and no coauthor
trailers. Formatting was performed by the automatic pre-commit hook.

The optimization policy changed to accommodate both engines in the existing
slots: P4 Applications and Service use `-Os` and LTO. LVGL control/layout code
follows that policy while draw sources retain `-O2`, verified in generated
compile commands. Drivers and standalone emulator components retain their
previous flags. No partition, C5 memory setting or SPI version changed.

This is app-only software validation. Script execution on the P4, native stack
headroom, UI timing and peripheral throughput still need physical acceptance.
No device, flash, SD card, microphone or cloud endpoint was accessed. See the
[script runtime documentation](../scripts/README.md) for language and API limits.

## Shared script ABI build record

Validated on 2026-10-19 from firmware source commit `c8be1773`, after automatic
pre-commit formatting. Both application-only targets passed the offline paired
build using ESP-IDF v5.5.3 (P4) and v6.0.1 (C5).

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,945,904 | 2,949,120 | 3,216 | `c601cf91551e682eba0c75ce924d8920a5a890eaf977a8dbbd0a41b5ffae7d06` |
| C5 | 2,016,048 | 2,097,152 | 81,104 | `f25f89e9d9f4bbd6aff4fee4a84be9b07f8099049d47995dfb4116b7257a91d0` |

`bash tools/highboy/test.command` passed without skips, including both real
interpreters under ASan/UBSan, generated ROM verification, capability denial,
argument bounds, typed records, binary reads, file cleanup, cJSON allocation
failures, consent denial/timeout, stale approval rejection, immutable loaded
source, binding failure and concurrent context registration. Screen callback
tests cover native navigation, ABI handoff and asynchronous deletion. Existing
RF, NFC, IR, Muse, Sub-GHz, preservation, DOOM and USB checks also passed.
Commitlint passed for the three implementation commits, whose messages contain
only titles and no coauthor trailers.

To retain the original slots, cJSON now uses `-Os` and LTO and the final P4 link
uses `-flto-partition=one`. Generated compile commands confirmed compact cJSON,
`-O2` for LVGL draw sources and unchanged driver flags. The P4 slot remains nearly
full. C5 source and memory settings, paired SPI v8 and partition layouts are
unchanged. The fresh C5 binary was embedded by the paired build; the hashes above
identify these exact build artifacts, not a reproducible-build guarantee.

The adapter shares `tos_api_t` with `.hb` apps and exposes Wi-Fi, BLE, read-only
filesystem, lifecycle, LED and basic widgets. Native pointers, C callbacks,
image/canvas rendering and unpublished radio tables remain outside the script
surface; see the [binding reference](../scripts/README.md#shared-firmware-abi).
This validates software paths with device fakes. Hardware radio sessions, display
responsiveness, stack headroom and cleanup on the actual P4/C5 still require
physical acceptance. No device, flash or SD card was accessed. Builds retain
pre-existing truncation and deprecated-library warnings; they are not warning-free.

## Companion NFC/RFID capture build record

Validated on 2026-10-19 from firmware source commit `5927bba0`, following the
automatic pre-commit hook. The new companion scope is native NFC-A/B/F/V
identification plus LF EM4100/EM4102 capture, status and retained-cleanup recovery.
Haptics is deferred by the user; companion card writes/emulation are not added.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,947,712 | 2,949,120 | 1,408 | `27d5aa89758a9d16185bde657c591a87c46be33be4584bded5468142a82a317b` |
| C5 | 2,016,048 | 2,097,152 | 81,104 | `66cbea060f7592f6bbe7e39ac915a82a9c89be8688d63a76dcf1e02b32ed845b` |

The offline paired build passed with P4 ESP-IDF v5.5.3 and C5 v6.0.1. SPI v8
headers remain byte-identical; Muse retains category `0x11`, with new P4-local
NFC/RFID categories `0x12`/`0x13`. Partition sizes and C5 memory settings are
unchanged. No additional optimization policy was introduced for this batch.
The final P4 slot is almost full; these hashes identify this exact build rather
than a reproducibility guarantee across changed build metadata.

`bash tools/highboy/test.command` passed without skips. New ASan/UBSan harnesses
exercise production RF handlers, NFC probes and host-link routing: A/B/F/V and LF
result layouts, malformed arguments, output bounds, shared-antenna contention,
startup errors versus card absence, failed OFF/no-sleep recovery, and rejection
of unauthenticated, failed-verification and stale-generation commands. The crypto
and hardware operations are explicit fakes in routing tests. Existing concurrent
RF tests additionally reject a foreign task's same-mode claim, OFF, deinit and
unreserve. CLI tests use a fake link and never open a serial port.

The existing JS/Lua, Muse, Sub-GHz, NFC, IR, preservation, DOOM DMA and USB tests
also passed. Commitlint passed for both implementation commits. Formatting was
performed by the automatic hook; imported source corrections retain their hash
chains in `integration/corrections.json`. Builds retain existing warnings.

No device was accessed or flashed. Live USB/BLE capture, antenna behavior,
interrupted hardware transactions and stack/timing headroom still need physical
acceptance. See [the command reference](../host_link/rf-commands.md).

## App-signing trust configuration build record

Validated on 2026-10-19 from firmware commit `1fa0778b`, after automatic hook
formatting. The default development configuration was rebuilt with both pinned
SDKs (P4 v5.5.3, C5 v6.0.1) after a custom-public-key build.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,947,712 | 2,949,120 | 1,408 | `db8b571208491be962afb05135f440764d0d1182f186b257baf899836973500e` |
| C5 | 2,016,048 | 2,097,152 | 81,104 | `2f3a7dbf176845d141cf169d25dda22e51d297eb45f2134ee2998c7079009097` |

The six new trust configuration tests passed: development default, required
production key, development-key rejection (both hex cases), malformed/zero keys,
selected-key header compilation and repeated reconfiguration of one build
directory. The complete `tools/highboy/test.command` suite passed without skips;
commitlint and whitespace checks passed. Partition layouts, paired SPI v8, C5
memory policy and compiler optimization settings remain unchanged.

An additional P4 build with the RFC 8032 public test vector selected in production
mode passed the app-slot check. Its binary contained the selected public key and
excluded the development public key. This selection check preceded hook
formatting; the final paired build above followed it and restored the default.
The final P4 binary contains the development key and excludes the test key,
confirming the wrapper resets cached selection when configuration is absent.
The P4 remains nearly full, with 1,408 bytes free. Existing build warnings remain.

No real production key was created or provisioned, and no private key, device,
flash or SD card was accessed. This validates configuration and compilation;
it does not add cryptographic certification, Secure Boot, key custody or physical
acceptance. Issue #149 remains open for radio API publication/teardown and the
real production identity. See [trust configuration](../app_sdk/README.md#keys--trust-store).

## Shared airtime and native RF ABI build record

Validated on 2026-10-19 after automatic hook formatting, from firmware commit
`555742ec`. Both pinned SDK builds and `tools/highboy/test.command` passed.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,927,584 | 2,949,120 | 21,536 | `d3d572fca9dc815b4a487ed845f6b3a223ba8cd3015300d34325222dce87b894` |
| C5 | 2,016,048 | 2,097,152 | 81,104 | `a87bc198e273af8e44838873f009a4d7b534f96cf76924a1dab2179e41b48cd1` |

This batch adds shared SX1262 airtime pacing, the P4 legacy-RNode transport gate,
portable SDK byte counts and header-matched bundle ABI versions, and native
NFC/RFID discovery in ABI 1.10 with JavaScript/Lua bindings. The local broker runs
on the persistent RF worker; native force-kill waits for local operations and a
failed kill claim no longer leaves natural app exit permanently suspended.

New tests cover airtime vectors, occupied-bandwidth/cooldown boundaries and
concurrent debits; rejection of P4 UART access; actual SDK script execution with
paths containing spaces; RF capabilities, cancellation, time bounds, A/B/F/V and
LF result conversion; broker copies, queue failure and force-kill quiescence;
production wire/local routing separation; and cleanup retry. Real interpreters
exercise both RF bindings and reject older ABI tables before accessing appended
pointers. Existing preservation, RF/NFC/IR, Muse, Sub-GHz, USB and DOOM tests pass.

The unchanged vendored libsodium 1.0.22 was also configured in a temporary host
build with `CFLAGS=-Os`, assembly disabled and `make check`: **101/101 passed**,
no skips. P4 now compiles that backend with `-Os`, recovering 23,408 bytes against
the same airtime-guarded candidate before ABI additions. The final P4 retains
21,536 free bytes. This changes code-size policy, not algorithms or vendor files;
physical crypto throughput, stack headroom and timing remain unmeasured.
C5 memory settings, SPI v8, partition sizes, existing driver/render/emulator
optimization flags and trust keys are preserved. Existing build warnings remain.

All eight SDK examples, including `rfread`, compiled as RV32 ET_REL objects with
the pinned compiler and no undefined external symbols. Commitlint and whitespace
checks passed. Example compilation and host fixtures do not prove card capture,
network throughput, field shutdown or a completed integration matrix. No device,
SD, flash, real production key or eFuse was accessed. The [matrix](MATRIX.md) and
[field guide](FIELD.md) identify missing implementations and physical acceptance;
no remaining issue is claimed fully closed by this batch.

## Meshtastic MQTT bridge build record

Validated on 2026-10-19 after automatic hook formatting, from firmware commit
`0132fb58`. Both pinned SDK builds and the complete offline regression suite
passed. SPI v8 headers, all eight C5 memory settings and the original partition
sizes remain intact.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,935,040 | 2,949,120 | 14,080 | `764cf819eaefcc830f601ce997eb45729299243d13c7bc3f893e5fc7baae7f21` |
| C5 | 2,041,216 | 2,097,152 | 55,936 | `891fb2fec8047b2cd78e89f6dd4954a3d64e35312cbeb295c16ea8ed598636c4` |

The linked P4 ELF contains `ui_lora_mqtt_open` and the MQTT packet parser; the
Meshtastic home opens that screen and its screen ID belongs to the LoRa family,
so navigation retains the active session. The C5 links the hash-pinned ESP-MQTT
client; the P4 reaches it through the mirrored category-0x14 SPI contract.
The final MQTT topic formatting checks introduce no new build warnings.
Existing unrelated warnings remain.

`tools/mqtt/test_mqtt.py` runs the actual codec, C5 service and P4 worker with
ASan/UBSan. It covers official field-5 MeshPacket/ServiceEnvelope vectors,
truncated protobufs, 20,000 malformed inputs, invalid UTF-8, allocation/register/
start failures, matching/denied SUBACK, disconnect state, bounded fragmented RX,
head ACK, full queues, duplicate TX, encrypted-envelope chat routing, self-echo
filtering, stop retries and a lost START response followed by cancellation.
Native screen routing is also checked; MQTT dependency resolution is controlled
by the firmware manifest rather than a test-time source hash inventory.

Network, SPI, RTOS and AES implementations in this host harness are explicit
fakes. The AES fake checks key size and nonce layout, not cryptographic output.
These tests do not establish real broker/TLS interoperability, radio delivery,
shared-chip performance or device reconnect behavior. No device, flash, SD,
private production key or eFuse was accessed. MQTT is now implemented for the
native Meshtastic screen, but #145 remains open for usable P4 RNode transport
and acceptance; #155 still includes remote GATT. See [MQTT](../mqtt/README.md).

## Remote GATT build record

Validated on 2026-10-19 after automatic hook formatting, from firmware commit
`7aaae6b0`. Both pinned SDK builds and the complete offline regression suite
passed. The original app partitions, paired SPI v8 headers, C5 memory overlay,
Muse integration and existing optimization policy remain intact.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,938,768 | 2,949,120 | 10,352 | `f971d8bbceb6d81503ecd284c5aa967cb523e3acd22402b3db74b04106b0c791` |
| C5 | 2,042,896 | 2,097,152 | 54,256 | `cdcdf57e166efbcb1efbb5f67a61372dcace3611775746119044bb25eb3e405f` |

The [remote GATT explorer](../gatt/README.md) now has an actual Detect-menu route,
a P4 background scan/poll worker, C5 service/characteristic discovery and bounded
ATT reads. The old reference catalog, JSON-only backend and self-freeing task
stack are removed. A C5 lease expires abandoned connections. Competing Bluetooth
operations cannot stop the owner; Muse still permits status/cancellation.

New ASan/UBSan host tests compile the actual C5 state machine and P4 proxy with
explicit NimBLE/event/transport fakes. They cover discovery, errors with NULL
results, readable-handle validation, denied reads, value/cache truncation, UUID128
copying, stale generations, allocation/timer failure, queued cancellation, lease
expiry and NPL release/reinitialization. P4 tests reject malformed lengths,
invalid states/counts/value lengths and transport errors. Menu routes are checked;
paired builds validate actual SDK APIs. Existing MQTT, RF, scripting, preservation,
Muse, USB and DOOM tests pass. These tests do not connect to a real BLE device.

This completes the code changes tracked by #155: native Wi-Fi/Bluetooth paths,
real Meshtastic MQTT/position paths and JS/Lua Dev Scripts. Physical Bluetooth,
networking and cross-interface acceptance remain under #143/#156; HLE, HID
layouts and haptics remain excluded. No device, SD, flash, key or eFuse was touched.

## Decoded IR ABI build record

Validated after automatic hook formatting on 2026-10-19, firmware `405a62f2`.
Both pinned SDK builds, paired source/configuration contracts and the complete
offline regression suite passed. Partition sizes, C5 memory settings and paired
SPI v8 are unchanged.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,941,888 | 2,949,120 | 7,232 | `844bd902a21ff0bd631ac34fd37d65fd416b276b1b2388aba45b9ea89b2faf42` |
| C5 | 2,042,896 | 2,097,152 | 54,256 | `98ed6eabdf783a4376047e9e01f81d3011c7436ce0c12865bbb46c5f3723382a` |

ABI 1.11 appends decoded IR receive/send after the existing RFID pointer. Native
apps and real JS/Lua interpreters share the table, capability approval and bounded
persistent-worker backend. Foreign native captures and torch prevent acquisition;
while acquired, foreign initialization/teardown is rejected. Failed RMT teardown
retains handles, resource grants and the app-worker lease until cleanup succeeds.

New ASan/UBSan tests compile the real lease, driver lifecycle wrappers/teardown,
IR backend and typed ABI against explicit RMT/task fakes. They cover RX/TX
permissions, protocol conversion, contention, stop before/after locking, receive
timeout and retained RX/TX/encoder cleanup. Broker tests now also route ten-byte
IR work on the persistent worker, preserve the two-byte NFC/RFID input bound,
and propagate IR errors. Real JS/Lua tests exercise both bindings, TX denial and
older-ABI rejection. Existing regressions and commitlint pass.

These tests do not emit or capture infrared on a device. Raw/file/AC pointers
remain NULL; Sub-GHz/LoRa adapters, production keys and physical acceptance remain
open. The P4 has 7,232 bytes free in its existing slot. No device, flash, SD,
private production key or eFuse was accessed.

## Companion ephemeral pairing build record

Validated on 2026-10-19 after automatic hook formatting, firmware `a18fbe7d`.
Both pinned SDK builds and the complete offline suite passed. Paired SPI v8,
all eight C5 memory settings and the original partition sizes are unchanged.
The firmware vendor dependencies and production trust keys are unchanged.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,942,496 | 2,949,120 | 6,624 | `033e753ee9130b3482f0c9b019c3e991c8c3983c126538156a708c473daa929e` |
| C5 | 2,042,896 | 2,097,152 | 54,256 | `6904b6010f25cc9575ae3a4697a5b02e2567e38711381272f1c83165a4262455` |

The [handshake v2 contract](../host_link/pairing.md) requires fresh X25519 keys,
PSK-authenticated transcript proofs, token-bound physical confirmation and signed
FINISH/READY before command authorization. There is no legacy PSK-only downgrade.
The existing HMAC envelope retains visible payloads; this is not payload encryption.
Both USB clients now verify reply MACs and replay counters. Confirmed USB pairing
requests BLE relay shutdown, making the C5 available for central GATT operations.

The new ASan/UBSan harness compiles production security with real libsodium
X25519 and OpenSSL HKDF/HMAC adapters; RTOS/NVS/RNG/identity are explicit fixtures.
It checks RFC 7748 public/shared vectors, low-order keys, altered proofs, token
replacement, denial, expiry, reset, reflection/replay and persistence failure.
The real Python cryptography helper matches C keys and comparison code; both
actual USB clients complete the exchange against that backend and reject forged
or replayed responses. The real UI OK/BACK handler is compiled and checked for
displayed-token approval and cancellation. Existing RF dispatch tests also check
FINISH routing without command dispatch. CI installs the host crypto dependencies.

Physical buttons, entropy quality and USB/BLE interoperability remain acceptance
work under #156. Muse's independent pairing is unchanged. No device, SD, flash,
private production key or eFuse was accessed. Commitlint and whitespace checks pass.

## Native radio observation ABI build record

Validated on 2026-10-19 after automatic hook formatting, firmware `e10ba682`.
Both pinned SDK builds and the complete offline suite passed. Paired SPI v8,
the C5 memory policy in `sdkconfig.defaults` and original app slots are unchanged. Commitlint and
whitespace checks passed for the implementation commits.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,944,736 | 2,949,120 | 4,384 | `3165d2bd5dfb5bf8b24de59e36fcd6947f1d44c15dcd7f651539294d19b1b0f8` |
| C5 | 2,042,896 | 2,097,152 | 54,256 | `4a72270ffcd190c61d5a9446a49e302a89d56982d1097d28bc3aace027db78a5` |

ABI 1.12 appends Sub-GHz and LoRa namespaces after IR, preserving older offsets.
Only `subghz.rx_poll` and `lora.mesh_poll` are implemented: they copy existing
native results/history, require RX approval and hold the local RF kill gate
while accessing native mutexes. Cursors are private to each native app/script
run. Polling neither starts nor stops a radio. All active-control entries in
these tables remain NULL. JS/Lua call the same tables and reject older ABIs.

The Sub-GHz worker now keeps its grant, DMA memory and callback queue until RMT
disable/delete succeed, preserving startup cancellation and rejecting competing
starts. New ASan/UBSan tests compile the real worker against explicit RMT/RTOS
fakes, including late callbacks during repeated cleanup failure. Capture IDs
persist across restarts. Radio-view tests cover permission denial, stop before
and after locking, initialization/lock failure, bounded copies and private
cursors. Native chat-ring tests cover simultaneous first mutex creation, 2,000
concurrent writes, overwritten backlog, independent cursors and sequence wrap.
The actual JS/Lua engines pass record/null/nil/error, permissions and ABI tests.

### Linked RAM checkpoint

These are **static linker-map counts**, not live free heap, task stack margins
or runtime performance measurements. They do not close #157.

| Target / linker region | Region bytes | Linked bytes | Unallocated bytes |
| --- | ---: | ---: | ---: |
| P4 DIRAM | 576,464 | 401,830 | 174,634 |
| C5 HP SRAM | 320,928 | 254,915 | 66,013 |
| C5 LP SRAM | 16,384 | 24 | 16,360 |

Reproduce with the lane's `python -m esp_idf_size`: IDF 5.5.3 uses
`--format json --target esp32p4 firmware_p4/build_highboy/TentacleOS_P4.map`;
IDF 6.0.1 uses `--format json2 firmware_c5/build_highboy/TentacleOS_C5.map`.
Unallocated linker memory can still be consumed/reserved during boot; runtime
heap minima, stack watermarks and throughput require the device.

Issue #151 was closed on GitHub after its completed pairing implementation.
Nine issues remain in this scope (#135, #143, #148, #149, #152, #153, #156,
#157, #158); #64 stays excluded. This observation subset advances #149/#156
but does not complete active radio control/teardown, production trust identity,
physical acceptance or a reviewed restore package. No device, SD, private
production key, flash or eFuse was accessed.

## P4 portable crypto LTO checkpoint

Validated on 2026-10-21 against the working tree based on `0085b5a3`, including
pending ESP SCAN/updater changes. The baseline and selected P4 builds use the
same sources except for the libsodium compile policy. The original P4 slot
margin grows from 16 to 10,320 bytes: **10,304 bytes recovered**. The utility
translation unit stays outside LTO to keep wiping/comparison calls opaque.
See the [full size/static RAM comparison](SIZE.md).

The selected paired build uses P4 IDF v5.5.3 and C5 IDF v6.0.1:

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,938,800 | 2,949,120 | 10,320 | `a14aa59e25799b325185c8941b2b9e2ac4e27e26410235d650635d11ab5e78d3` |
| C5 | 2,050,752 | 2,097,152 | 46,400 | `74a92a2f34fcecb58e2e7c6d11f4a8ce4806b4e64250ccb86a15993c0b4600e3` |

Both application builds and the complete offline runner passed, including the
pending Wi-Fi scan tests, paired SPI v8 and C5 memory contracts. The isolated
portable libsodium `-Os`/LTO build, with final `-fno-lto` for its utility object,
passed **101/101 tests**, zero skips/failures. Real C/Python companion pairing
also passed against that library with host LTO enabled, including vectors,
confirmation, expiry/replay and forged-response rejection. The usual full
suite uses its existing host dependency configuration. No vendor algorithm,
production identity, partition, driver or emulator policy was changed.

Static P4 DIRAM falls from 402,222 to 402,074 linked bytes; this 148-byte
reduction does not measure live heap or performance. The ELF and effective
compile commands were inspected for the utility LTO exclusion. No physical
bench/flash/eFuse action was performed. #157 remains open for runtime and
reviewed OTA/assets/recovery acceptance; no additional issue is closed.

## Bounded Sub-GHz ABI and native shutdown build record

Validated on 2026-10-21 after automatic hook formatting, implementation
`2229f227`, against the working tree including its pre-existing ESP SCAN/updater
changes. Both pinned application builds and the full offline suite passed.
Original app slots, paired SPI v8 and all eight C5 memory settings are preserved.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,940,784 | 2,949,120 | 8,336 | `f88bf946fe2c2694d6983155c443b51e8c092cf527c64e05488ff737d97b8971` |
| C5 | 2,050,752 | 2,097,152 | 46,400 | `9b846dc57c217765884faa1785ad513346019cc31fcd6c007cb53ff0a1b2d25b` |

ABI 1.13 adds one-shot Sub-GHz capture and bounded raw OOK transmission to signed
apps and JS/Lua. The shared RF worker owns hardware and retained failed cleanup;
no app-owned asynchronous task or new SPI operation is introduced. The existing
latest-capture observer and LoRa chat observer remain available. Async/file/raw
read/spectrum app methods and active LoRa remain unpublished under #149/#156.

The CC1101 lease reserves the radio without preemption and gates all 29 public
hardware/state operations. SPI errors are latched across legacy void setters.
Failed RMT/SPI/power cleanup retains buffers/handles and the reservation, while
pausing driver access even for unrelated commands on the same worker. A later
local call resumes ownership solely to retry cleanup before new hardware work.
Native TX now owns initialization and cleanup in its worker, waits for in-flight
senders, handles INT32_MIN conversion and retains active symbols until RMT is
quiescent. Native spectrum reserves the radio and discards partial sweeps.

New ASan/UBSan tests exercise the actual backend, lease, all driver wrappers,
SPI transaction helpers, native TX worker and spectrum against explicit hardware/
RTOS fixtures. Native TX uses a real host thread, covering stop during startup
and active transmission, setup/allocation failures, transmit timeout and repeated
channel/encoder cleanup failure. Other cases cover capture bounds, zero/corrupt
records, error latching, native contention/acquisition races, paused owner access,
no-sleep cleanup failure, invalid pulses and recovery of retained resources.

The real JS/Lua engines pass RX/TX approval, records, malformed/overflow CSV and
older-ABI rejection tests. Broker tests check typed callback scheduling/error
propagation and return to ordinary RF requests, retaining the legacy wire/local
routing boundary. Existing Wi-Fi, NFC/RFID, IR, MQTT, GATT, crypto/pairing, Muse,
USB, DOOM and preservation tests pass. Commitlint and whitespace checks pass.
The RX-only `subghz_capture` SDK example compiles to a RISC-V object with no
unresolved symbols; this does not prove installation or radio capture.

No physical radio, serial, SD, flash, production key or eFuse was accessed. RMT
wait limits exclude hardware setup, bus locks and recovery; target latency and
RF/power behavior remain physical acceptance. No additional issue is closed;
#149/#156 still include missing interfaces and production/device requirements.


## Queued LoRa mesh text build record

Validated on 2026-10-21 after automatic hook formatting, implementation
`91b6e18e`, against the working tree including its pre-existing ESP SCAN/updater
changes. P4 IDF v5.5.3 and C5 IDF v6.0.1 application builds and the full offline
suite passed. Original slots, paired SPI v8 and C5 memory policy are preserved.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,941,584 | 2,949,120 | 7,536 | `c9244d44e1b637a275bc29ca39f93f973eb0da047372e9a5f1324fa3e670e212` |
| C5 | 2,050,752 | 2,097,152 | 46,400 | `004e8227350a9c13ac963de488d722131e8e7176ebd89e2200ef99a069ebd516` |

ABI 1.14 publishes `lora.mesh_send_text` for signed apps and JS/Lua with TX
permission. It submits copied 1..159-byte text to the same four-entry queue used
by native and companion chat. Only the active protocol's native poll task drains
it, at most one item per iteration. Acceptance does not prove transmission or
delivery, and accepted messages survive the submitting app. Backend failures
consume the item, log an error and omit the outgoing chat record. Actual radio
operations continue through the existing shared airtime guard.

ASan/UBSan tests exercise production queue/ring/start functions with real host
threads: concurrent startup/producers, atomic protocol publication, failed start
recovery, bounded copied storage, FIFO order, queue saturation, wrong-protocol
consumer, backend failure and both mesh dispatch paths. Adapter tests verify TX
capability, unavailable/held cancellation gates, stop-after-lock and native error
propagation. Real JS/Lua engines verify approval, successful submission, backend
errors and ABI 1.13 rejection. All existing offline preservation and subsystem
tests pass. The TX-only `mesh_text` example compiles to a RISC-V object with no
unresolved symbols; no signed app installation or RF result is inferred.

No physical radio, serial, SD, flash, production key or eFuse was accessed.
Static queue storage adds 640 PSRAM bytes plus counters/flags; runtime heap,
stack, latency and airtime acceptance are unmeasured. Raw LoRa, startup/teardown/
node app methods, asynchronous/file/spectrum Sub-GHz and production/device
requirements remain open. This batch closes no additional issue.


## Bounded Sub-GHz spectrum build record

Validated on 2026-10-21 after automatic hook formatting, implementation
`7d128603`, against the working tree including its pre-existing ESP SCAN/updater
changes. P4 IDF v5.5.3 and C5 IDF v6.0.1 application builds and the full offline
suite passed. Original slots, paired SPI v8 and C5 memory policy are preserved.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,942,368 | 2,949,120 | 6,752 | `bf96f5122546ffb72eb46e9212a2ef1b3148ca4edc3f484b0a861403dd65bd76` |
| C5 | 2,050,752 | 2,097,152 | 46,400 | `fa3dec8fc911fd3e7e89a7a65429d1909c7ceac3c6c4f34872af25cf53e48129` |

ABI 1.15 appends one-shot `subghz.spectrum_capture`: 80 RSSI bins, 80 Hz..20 MHz
span entirely inside one supported band, RX capability, existing persistent RF
worker and non-preempting CC1101 lease. It uses preset 6, 400 us settle, three
RSSI samples 20 us apart with the native -130 dBm floor, and one tick yield per
bin. Results contain copied start/step frequency and 80 values. The call neither
starts the native spectrum worker nor overwrites its snapshot. SPI or cleanup
failure returns a zero result; failed cleanup retains ownership/power until a
later bounded operation retries successfully. No app pointer is retained.

ASan/UBSan tests exercise the actual backend/lease against explicit SPI/RTOS
fakes: exact idle/tune/RX/settle/sample/yield order, all bins and peak values,
minimum/maximum/odd spans, three-band boundaries, invalid/overflowing ranges,
capability/cancellation/readiness gates, contention, mid-sweep tune/read failure,
no partial publication and retained idle/power cleanup with recovery. Existing
native RX/TX/spectrum and CC1101 wrapper tests also pass.

Real JS/Lua tests cover 80-element records and each language's array indexing,
RX approval, backend error propagation, ABI 1.14 rejection and allocation-failure
cleanup across the JSON object/array/number path. The complete preservation,
crypto/pairing, NFC/RFID, IR, LoRa, Wi-Fi/BLE, Muse, USB and emulator suite passed.
Commitlint/whitespace checks pass. No physical radio, serial, SD, flash, key or
eFuse was accessed; RF resolution, latency, runtime heap/stack and interoperability
remain unmeasured. Sampling bounds exclude setup, bus locks and recovery.

This completes the bounded spectrum subset of #149. Asynchronous/file Sub-GHz,
raw LoRa and native mesh lifecycle/node app adapters, production identity and
physical acceptance remain pending. No additional issue is closed.


## ABI publication and signing acceptance (#149)

Validated on 2026-10-21 after automatic hook formatting, implementation
`5027cf7d`, against the working tree including its pre-existing ESP SCAN/updater
changes. Both pinned SDK application builds and the full offline suite passed.
Original slots, paired SPI v8 and the C5 memory policy are unchanged.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,942,368 | 2,949,120 | 6,752 | `9cf241d5da379bea6ca0db11f95a074d1e052fa21ba8b1ed920f0637f7009488` |
| C5 | 2,050,752 | 2,097,152 | 46,400 | `44fa545d1ef604947a87fa4da28c1c111efe5182e4115da79a4a5b9d9c3d91e5` |

The original #149 asks for radio structures in the ABI and production app trust.
Wi-Fi/BLE/NFC/RFID/IR/Sub-GHz/LoRa types and optional tables are published through
ABI 1.15. Their supported native/script operations are covered by the existing
capability, cancellation, ownership and older-ABI regression suites. Missing
optional operations stay NULL and are listed as further #156 interface work;
publishing a structure is not an assertion that all prospective endpoints exist.

The user explicitly deferred the actual production identity and requested
configuration support first. CMake selection replaces the development trust
store and rejects missing/malformed/zero/development keys in production mode.
The new signed-bundle test uses real generated trust headers, the unchanged
production `tos_hb_open`, real libsodium verification and independent Python
Ed25519 signatures. Public RFC 8032 seeds and the checked-in throwaway dev seed
are test fixtures, not production identities. All **158 checks** pass: selected
identity acceptance, default-dev acceptance, cross-store/dev/foreign rejection,
v2/v3 formats, capability/name/ELF/metadata/signature tampering and truncated or
invalid-length bundles. Fixtures are never loaded or executed as native apps.
The stale opener-header description was corrected to distinguish verified full
opening from unverified launcher scans; no firmware behavior was changed.

Full offline regressions, commitlint and whitespace checks pass. The real app
identity/custody remains a #157 release prerequisite; Secure Boot/Flash Encryption
remain #152, and cross-interface implementation/device acceptance remain #156.
No production identity, serial/device, RF, SD, flash or eFuse was accessed.
This reconciles #149 with the agreed configuration-first scope, superseding the
earlier notes that expanded it to all optional radio operations and physical tests.


## Recovery and power-policy hardening build record

Validated on 2026-10-22 after automatic hook formatting, implementation
`0d8d0b13` (recovery `f4662b33`), against the working tree including pre-existing
ESP SCAN/updater changes. P4 IDF v5.5.3 and C5 IDF v6.0.1 application builds and
the full offline suite passed. Original slots, paired SPI v8 and C5 memory
policy are preserved. Feature request #163 is excluded by user request.

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,943,120 | 2,949,120 | 6,000 | `a95bd0f4feba516ef39b7d027efc4df000d3a8a69cb1756a98f22b7a7ebd58fb` |
| C5 | 2,050,752 | 2,097,152 | 46,400 | `0cd33546b8dc0243d0bd6869c67512de7e04d234d37f67b9f47af41f715923c3` |

#153 fixes retained-dump recounting, NVS error handling, saturated counters,
atomic RTC count access, a boot-loop decision latched before the stable timer,
and core-summary/timer failure handling. A valid old coredump stays visible on
clean reboot without becoming a new abnormal reset. NVS read failures do not
create fabricated replacement values; write/commit failures are logged. Existing
historical totals are preserved, and multi-key persistence is best-effort rather
than a transactional guarantee. The recovery document now reflects the actual
integrated partition layout and guarded deployment contract.

#135 fixes initialization without a wake guard, lost pre-init USB/VBUS inputs,
false applied-hold state after errors, concurrent hold accounting and client
release of the manager's hold. Failed init cleanup remains owned for a later
retry; repeated inputs retry failed transitions. The integrated profile remains
PM-disabled. The enabled power-manager module also compiles with the real P4
compiler/includes; this is an isolated object check, not a PM-enabled firmware
or hardware sleep test.

New ASan/UBSan suites compile the actual modules with explicit NVS/RTC/timer/PM/
heap fakes, with coredumps and PM each enabled/disabled. Recovery cases include
stale dumps across clean/abnormal reset sequences, allocation/summary/erase and
NVS open/read/set/commit failures, overflow, timer create/start/delete failures,
delayed boot-loop decisions and real host-thread stable-count reads. Power cases
include guard/configure/cleanup failures and recovery, input retry, reference
overflow/over-release, and concurrent initialization/USB/client threads.

Both new suites, all existing offline regressions, preservation, commitlint and
whitespace checks pass. Source/proof snapshots confirm pending user edits were
preserved; the runner commits contain only the new suite lines. No device,
serial, RF, live NVS/SD/flash, production identity or eFuse was accessed. #135
and #153 remain open for real sleep/wake/current and reset/watchdog/recovery
acceptance. No issue is closed by this record.


## Native OTA and verified app-only candidate build record

Validated on 2026-10-22 after implementation commit `22a3e7a9`. The working tree
also contains the pre-existing ESP SCAN/updater changes; `source_dirty` is true,
and the manifest fingerprints the actual inputs. This is not a clean-commit
release or evidence that those pending user changes were committed.

Both pinned SDK lanes, paired SPI v8 and original app slots passed:

| Target | SDK | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| P4 | v5.5.3 | 2,942,848 | 2,949,120 | 6,272 | `e57d701db3312112ac40db144c39e3d03f86a4ecf280f4a0693331cae423855c` |
| C5 | v6.0.1 | 2,051,056 | 2,097,152 | 46,096 | `2e47806f35d64036dc546adf4c039c3e9693a50911b8fc9a951d2976efd4d667` |

`tools/highboy/package.py` completed both builds and the complete offline test
runner after the commit hook formatted the firmware. New ASan/UBSan fixtures
exercise actual P4 OTA file/flash/power failure paths, exclusivity, retained
abort/release cleanup and version metadata writes. Extracted actual C5 post-boot
validation covers bridge-health deadlines, state reads, confirmation and rollback
errors. Native update UI tests exercise worker/UI separation, allocation failures,
native progress/errors and deferred deletion/reopen; SDK/LVGL calls are explicit
fakes and do not prove device behavior.

Eight package tests cover partial publication, source corruption, immutable
existing candidates, invalid profiles/metadata, duplicate JSON, symlinks, reviewed
LTO symbol clones and selected public trust-header fingerprints. The standalone
verifier passed on:

`integration/release/candidate_e57d701db331_2e47806f35d6_0f23fac77384/`

The package contains P4/C5 applications and a manifest, with no bootloader,
restored assets, production provisioning or partition migration. Hash/profile
verification is not signature authentication, production custody or hardware
acceptance. The build selected the existing development app identity. Whitespace
and Conventional Commit checks passed for the implementation commits. Pending
user files and unrelated correction proofs were checked against initial hashes.

GitHub still lists eight in-scope issues: #135, #143, #148, #152, #153, #156,
#157 and #158. #163 and #64 remain excluded. No High Boy serial endpoint was
available; no target port was opened, no firmware was flashed and no live
SD/NVS/eFuse/security setting was changed. Real card/throughput/current/reset/
watchdog/cross-interface/runtime-performance acceptance, production identity and
reviewed two-chip security/restore deployment remain outstanding. The original
Bash report still lacks its command/error reproduction. No issue is closed by
this build record.


## Shared IR/NFC and offline security/recovery preparation build record

Validated on 2026-10-22 after implementation commit `53788e90`. This working tree
also includes the preserved, pre-existing Wi-Fi/updater changes: the manifest
records `source_dirty=true` and fingerprints actual firmware inputs. No clean
commit-only or hardware-tested release is claimed.

Both pinned SDK lanes passed with SPI v8, the original slots and C5 memory
overlay preserved:

| Target | SDK | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| P4 | v5.5.3 | 2,946,368 | 2,949,120 | 2,752 | `9a5f16878047a2dce8e140518812257ef22570a79b704b9d08274acfc3165b12` |
| C5 | v6.0.1 | 2,051,056 | 2,097,152 | 46,096 | `b70efec9cb046be4237c29758933a0059d015627c365a6b2769b00321f884f66` |

The complete offline runner passed after hook formatting, including the expanded
IR native lease/raw/file/AC tests, actual JS/Lua ABI 1.16/1.17 dispatch, native
Classic 1K block operations and authenticated routing/CLI tests. Host IR tests
exercise signed timing bounds, carrier/total duration, retained symbol buffers
across RMT failure, native contention, SD path/approval checks, real file parsing
and HEAT/DRY enum conversion. NFC fixtures exercise authentication/IO/readback
failures, manufacturer/trailer protection, unsupported card geometry, malformed
wire payloads and retained field/power cleanup. Hardware and protocol IO remain
explicit fakes; these tests do not prove RF/card behavior.

Five security-auditor tests and real `kconfgen` resolution/auditing on both pinned
SDKs passed. The generated profiles are isolated review artifacts, with no
private signing-key selection or development configuration changes. No signed
secure application/bootloader or production identity was produced. The current
baseline-only projection with 64 KiB secure padding plus a 4 KiB signature sector
is 2,953,216 P4 bytes and 2,101,248 C5 bytes: each exceeds its original slot by
4,096 bytes. A real secure build can change size further; this remains a code-size
and release-preparation gate rather than a request to enlarge partitions.

Six private-recovery tests passed, covering paired backup hashes/maps, strict
OTA state, private permissions, malformed/tampered/fabricated metadata, missing
peers, symlinks, candidate corruption and image-validator failures. The additional
`test_recovery_images.py` passed with real esptool and both actual candidate apps
in synthetic full-flash fixtures. Boot containers are deliberately non-bootable
and asset bytes deliberately are not a filesystem; recovery approval, identity,
security and healthy-assets flags remain false. No actual device backup was read.

The packager completed both builds and all offline regressions and published:

`integration/release/candidate_9a5f16878047_b70efec9cb04_4d3774f2eff1/`

Standalone candidate verification passed. This is still an app-only candidate;
the recovery tool separately prepares private review bundles from explicit full
backups and has no flash execution path. The production/security/asset/restore
and runtime acceptance boundaries remain in [RECOVERY.md](RECOVERY.md) and
[the issue audit](ISSUES.md). Firmware commits use Conventional Commit titles
only, without coauthors; whitespace and commitlint checks passed. Pending user
files/proofs were checked against snapshots and preserved. Both protocol copies
remain identical; only the shared NFC IDs were committed, leaving the prior
pending Wi-Fi extension outside these commits.

No serial port, RF device, live SD/NVS/flash, production key or eFuse was accessed.
No issue is closed by this record. Optional NFC emulation/session, asynchronous/
file Sub-GHz and raw/session/node LoRa adapters remain code work under #156;
release size/identities/restoration and the original Bash reproduction remain
separate unfinished requirements. #163, #64, HLE and haptics remain excluded.


## Sub-GHz shared raw and file replay build record

2026-10-22, code revision `ff4574cf318b743bcc9ccd6fb4e486018e130339`, ABI 1.19.
The final documentation-only follow-up does not alter firmware sources.
The packaged worktree includes the user's pending Wi-Fi/UI work, so the
manifest truthfully records `source_dirty=true`.

App-only candidate: `integration/release/candidate_65ccced3011b_07e0744a04a4_c625be1a2f71/`.

| Target | Application bytes | Original slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,947,888 | 2,949,120 | 1,232 | `65ccced3011bc85649b4c321b57dc75c2925ec41e91503a2bd025d2b0667e0c8` |
| C5 | 2,051,056 | 2,097,152 | 46,096 | `07e0744a04a4f2de265715a6812bac39c5090cbea0ae4eda382710c7f7eab27d` |

`tools/highboy/package.py` passed the full offline runner and both pinned IDF
builds (P4 5.5.3, C5 6.0.1), then verified and atomically published this pair.
The standalone package verifier and real-esptool recovery image tests also
passed. The latter uses synthetic boot containers/assets with the actual built
apps; it does not certify boot or a real device backup.

Targeted tests cover global complete raw snapshots, missing/short capacity,
capability/stop gates, failed-cleanup history preservation, RAW/decoded replay,
legacy and long-name encoders, strict metadata/key overflow, excess pulses,
malformed paths, NUL/oversize files, allocation failures and retained RMT/SPI
cleanup. Both actual JS/Lua interpreters cover the new bindings, older ABI
rejection and combined TX/filesystem approval. The radio-table fixture was
updated for the newly published entries. Import preservation and commitlint
passed; formatting was performed only by the pre-commit hook.

Paired SPI remains v8 with matching headers and the C5 memory policy in `sdkconfig.defaults` preserved.
A balanced-LTO experiment produced `0x2d3220` bytes on P4, exceeding the original
slot by `0x3220`; it was fully reverted. The selected pair retains the previous
compiler policy. No physical RF/SD/flash test or production provisioning ran.
Remaining async Sub-GHz, NFC emulation and full LoRa lifecycle adapters are code
work, not host-test acceptance or issue closure.


## LoRa copied nodes and bounded receive build record

2026-10-22, executable source revision `6d3ce5e7e7b7adb952ac18244fecacf7f5adf373`,
ABI 1.20. The documentation follow-up changes prose/header comments only; no
executable declarations/statements change. Pending user Wi-Fi/UI changes remain
in the worktree and the manifest correctly reports `source_dirty=true`.

App-only candidate: `integration/release/candidate_558a33d25d9c_bc26ce7b2fab_b29382bb7d99/`.

| Target | Application bytes | Original slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,946,864 | 2,949,120 | 2,256 | `558a33d25d9c6f7f18ab9bfaa7fbaa6de5f1e1922ee0ac5e1cc78a4335541fb8` |
| C5 | 2,051,056 | 2,097,152 | 46,096 | `bc26ce7b2fabbd4bb7f0b5a9e33af22a4e5978c91ff38c03ea657245f35e3660` |

`tools/highboy/package.py` passed the full offline suite and the pinned P4
5.5.3 / C5 6.0.1 builds before atomic publication. The standalone verifier and
real-esptool recovery image tests passed with this pair too. Recovery fixtures
use synthetic boot containers/assets with the actual built apps, not real
device backups or boot acceptance.

The new `tools/lora_nodes/test_nodes.py` compiles the actual Meshtastic/MeshCore
databases, session copy routines and receive handlers with ASan/UBSan. Concurrent
readers/writers verify whole-record copies and bounded/terminated names; cases
cover mutex allocation/timeouts, removed contacts/path updates, malformed
protobuf lengths, non-terminated DM plaintext and oversized group/direct/path
payloads. Crypto, NVS, FreeRTOS and radio operations are explicit fakes.
Actual JS/Lua tests and native API tests cover node/count results, capabilities,
old ABI rejection, invalid/missing indices and backend errors. Existing native
chat startup/queue/ring and resource-manager tests remain passing.

Generated compile commands confirm final `-Os -flto=2 -ffat-lto-objects` for
Core/resource_mgr and the unchanged `-O2` CC1101 policy. Crypto utility compilation
still ends with `-fno-lto`. Emulators, C5 policy, shared protocol v8 and all eight
C5 memory settings remain preserved. Core-only optimization recovers 2,464 bytes;
the final batch retains 1,024 more P4 bytes than its starting ABI 1.19 pair.
A link-only no-inline-once experiment did not help and was fully removed.

Import preservation and Conventional Commit checks passed. Only the pre-commit
hook formatted firmware. No device, production identity, eFuse or serial port
was accessed. This remains an app-only development candidate; async Sub-GHz,
NFC emulation/session and LoRa raw/start/stop adapters remain code work.


## Owned Sub-GHz sessions and forced teardown build record

2026-10-22, executable source `5dda671042bb4b1066f393cc37242a0063dd98ee`, ABI 1.21.
The follow-up documentation commit changes no firmware source. Pending user
Wi-Fi/UI/guarded-updater work is present; `source_dirty=true` is intentional.

App-only candidate: `integration/release/candidate_0661c47417cc_1c811f394882_f1054f1ddbf6/`.

| Target | Application bytes | Original slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,948,592 | 2,949,120 | 528 | `0661c47417cc573717d733229530b99029be82e67e92e95d6488ae2261dd02aa` |
| C5 | 2,051,056 | 2,097,152 | 46,096 | `1c811f39488227d622d5b9695b484ac6e8c08acedfacc3957736c099aee106a8` |

The packager passed both pinned builds (P4 IDF 5.5.3, C5 6.0.1), the complete
offline runner and profile/integrity verification before atomic publication.
The standalone verifier and real-esptool recovery image tests passed. The latter
uses synthetic boot containers/assets with actual built apps, not real device
backups or boot acceptance. Protocol v8, paired headers and all eight C5 memory
settings remain aligned. No flash, radio, SD card, production identity or eFuse
was accessed.

The actual Sub-GHz backend/service task runs on a host thread with ASan/UBSan
and explicit RMT/SPI/FreeRTOS fixtures. Tests cover empty RX readiness, unread
frame preservation across empty windows, RX/spectrum polling, retune, short
buffers, foreign/stale/reused task identities, ID exhaustion, startup starvation
and busy radio, stop while should_stop is set, permission errors, generic/SPI
errors, no partial sweep, non-owner cancellation and retained cleanup retries.
Context tests verify bind/unbind cancellation hooks across concurrent task bindings.
Actual JS/Lua tests cover all five session bindings, result records/empty polls,
old ABI rejection, permissions and native errors.

Forced-stop tests execute the production manager/helper with scheduler and
UI/RF/SPI fixtures. They verify cross-core suspension acknowledgement, lock and
suspension rollback, self-stop rejection, and context/grant cleanup before
task deletion with immediate identity reuse. Natural-exit retry tests remain
passing. The pinned IDF task implementation was checked locally: its state
query reports a task running on either core before returning suspended. This
supports the software ordering; hardware timing remains unmeasured.

The source-matched comparison restored only the prior Drivers CMake options,
held executable source at the same commit, built, recorded the image and then
restored the selected options in a finally block. Baseline P4 is 2,950,272
bytes (1,152 over the original slot), SHA-256 `ee900e7e34aa3efe67f2277b032032b0a4415df46187f901f517b9b3adf34b77`; its size check
correctly fails and it was never packaged. The selected final `-Os` control
policy saves **1,680 bytes**, producing the 528-byte development margin above.
Generated compile commands confirm no LTO for those nine control sources, while
CC1101 transfers, NFC IRQ/SPI HAL and crypto utility opacity retain their prior
flags. No feature or partition was removed.

Commitlint, whitespace and import-preservation checks passed. Firmware formatting
was performed only by the pre-commit hook. The tiny P4 margin does not solve
secure padding or reserve room for remaining NFC/LoRa adapters; no physical
acceptance or issue closure is claimed by this batch.


## NFC and LoRa owned lifecycle build record

Executable source: `9fbbd97d0a271bae32636d215b60329f17923fd2`, ABI 1.23.
The package includes the preserved pre-existing Wi-Fi/UI/updater work and records
`source_dirty=true`. Subsequent documentation-only commits do not change these
binaries. Verified app-only candidate:
`integration/release/candidate_96dd00e16235_ef9b35adf7d0_d6622488c52e/`.

| Target | SDK | App bytes | Original slot | Free | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| P4 | v5.5.3 | 2,942,224 | 2,949,120 | 6,896 | `96dd00e1623589415765a169b09a81b49a719797f70e48f64e8c2a27206635db` |
| C5 | v6.0.1 | 2,051,056 | 2,097,152 | 46,096 | `ef9b35adf7d0b8362dd9e42ded137dec3fdda922b933dd6138b669c5dcb5316f` |

`tools/highboy/package.py` passed both exact pinned builds, the complete offline
suite, original app-slot and eight-setting C5 overlay checks, paired protocol-v8
headers, linked preservation checks and atomic package publication. The unchanged
runner fingerprint is
`3b7ab188670d774171e88207e63b57a8b1b425838e3e3562bc30ad5c0747a4bc`;
its existing RF/radio ABI runners now invoke the new production lifecycle tests.
The standalone package verifier and real-esptool recovery-image fixture tests
also passed. The expected wrong-chip warning belongs to a negative fixture;
synthetic boot/assets fixtures are not physical boot/restoration evidence.

New ASan/UBSan tests execute production NFC app/listener startup, cancellation,
foreign/stale owners, retained failed-OFF cleanup, task creation and setup failure,
and SPI/IRQ failure injection at every target configure/listen/start operation.
Production LoRa raw/mesh wrappers cover parameter/capability rejection, valid
packet copies, corrupted-packet filtering, actual TxDone versus TX timeout,
preflight transmit refusal, native-owner refusal, cancellation and retained HAL
cleanup. Actual native session stop/owner functions, poll/TX/IRQ stop routines,
phone-bridge stop functions and the stream barrier run against delayed/failing
acknowledgement fixtures. Context bind/rebind/unbind checks all three cancellation
hooks. Real JavaScript/Lua tests exercise the new NFC/LoRa methods, typed results,
empty polls, permissions, bad hex/power and older-ABI rejection.

Commitlint and commit-range whitespace checks passed. Pre-commit formatting was
performed by the configured hook, never by a manually invoked formatter. All
pre-existing user files and correction proofs were hash-audited as preserved;
the SPI/TESTS documentation appends were staged separately from user edits.
No push, device access, flash, production signing identity or provisioning was
performed. Physical RF/mesh, C5 transport, memory/stack and timing acceptance
remain unverified. #156 completes the requested source/interface matrix without
waiting for physical acceptance; this is not a production release or resolution
of #152/#153/#157.


## Operational recovery and bounded boot audio build record

Issue #153 is complete at the user-requested software boundary, without waiting
for physical acceptance. Executable source is `7ae39a52a770eb054ba2762a3bad89f23cc8587d`;
the worktree includes the pre-existing user changes (`source_dirty=true`). The
four modular source/test commits are `f09f5f5a`, `b37af0d2`, `e60793df` and
`7ae39a52`. The final documentation commit does not change executable source.

The paired pinned builds, full offline host suite and preservation checks pass.
Immutable app-only candidate:
`integration/release/candidate_7a4792d488ec_f813339e1fc9_6cd1c5f3be7a/`.

| Target | Pinned SDK | App bytes | Original slot | Free bytes | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| P4 | v5.5.3 | 2,946,960 | 2,949,120 | 2,160 | `7a4792d488ec34da03abac1aad84ab65ba1dd54350997b6b02273f2a5dc572d2` |
| C5 | v6.0.1 | 2,051,664 | 2,097,152 | 45,488 | `f813339e1fc9059baa6e8b3a1e4eb95975e32722599b7c5defcd0272884a6057` |

`verify_package.py` validates exact hashes/profile/slots. The real-esptool recovery
image tests pass, including the deliberate wrong-chip rejection. Commitlint and
whitespace checks pass; source formatting ran only through the pre-commit hook.
Paired protocol v8, original partitions and the C5 memory policy in `sdkconfig.defaults` are preserved.
The P4 slot remains nearly full; this is an unsigned development candidate, not
completion of the secure signing/release-size work in #152/#157.

`tools/boot_report/test_boot_report.py` remains part of the existing full runner
(fingerprint `3b7ab188670d774171e88207e63b57a8b1b425838e3e3562bc30ad5c0747a4bc`).
It now runs ASan/UBSan checks against production P4/C5 monitor code, boot report,
P4 kernel/app entry, bounded boot sound, I2S control and factory reset/recovery
functions. Explicit fakes inject scheduler/task/WDT/HAL/NVS/decoder failures.
Tests cover healthy-completion/stable-timer gating, first-render stalls, task
identity reuse, checked WDT APIs, panic escalation, NVS preservation, headless
UI gating, no recovery OTA confirmation, bounded decoder progress/format/size,
low heap, missing file, PCM volume, short/zero/odd DMA writes, bounded drainage,
failed stream-stop retention, SD mount-on-request, filesystem/NVS reset errors,
failed worker creation and failure without reboot. Temporary host directories
alone supply file fixtures; no installed assets, card, NVS or flash are touched.

Physical watchdog timing, a reproduced device crash, flash-coredump capture and
offline decoding, cache behavior, actual Helix decoding and audible playback
remain unverified. Optional `/assets/storage/ghost_cyberpunk_boot.mp3` is not
installed by this package. Recovery/low-heap/missing-file cases skip boot audio.
No device was accessed, flashed or provisioned, and no changes were pushed.
User Wi-Fi/deployment changes and their provenance were preserved separately.


## Paired factory-security build and crypto record

Issue #152 is software-complete at the requested boundary without physical
provisioning. Commits `d19f58bd`, `ab804ef8`, `5c102eaf` and `ebe7d5c2` deliver the
isolated factory profiles/build auditor, external signature verifier, real-image
RSA/XTS tests and pinned-lane CI integration. This documentation commit changes
no executable source. No production signing identity was supplied or generated.

Actual factory builds used P4 ESP-IDF v5.5.3 and C5 v6.0.1. Their reports record
base source `2e5bfe3ee699a0031902c91d67c5776467329a63`, `source_dirty=true`, including
the pre-existing user changes and the security-profile implementation now
committed above. Reviewed outputs remain outside the checkout:
`/private/tmp/tentacleos-152/p4-factory-v2/` and
`/private/tmp/tentacleos-152/c5-factory-v2/`.

| Target/image | Actual padded unsigned bytes | Bytes with signature sector | Capacity | Signed free bytes |
| --- | ---: | ---: | ---: | ---: |
| P4 app | 2,883,584 | 2,887,680 | 2,949,120 | 61,440 |
| C5 app | 1,900,544 | 1,904,640 | 2,097,152 | 192,512 |
| P4 bootloader | 40,960 | 45,056 | 57,344 | 12,288 |
| C5 bootloader | 40,960 | 45,056 | 57,344 | 12,288 |

The real-image tests append and verify actual RSA-3072 signature sectors, not
only size projections. Fixture signatures are randomized and removed with their
temporary keys; these are not production artifacts. Unsigned source hashes:

| Target | App SHA-256 | Bootloader SHA-256 | Factory table SHA-256 |
| --- | --- | --- | --- |
| P4 | `07b2819bd6c2f1732b7bfeb72657ccd645f3a01752254810b239470f7dd1e861` | `e2e1b7f59856451d7b06497853609bac1344c605f04bc0034085be3be05ae4d7` | `0ac4b47148174d0496574239447459aaba4a446be2bc3c30507f7ebc950e5d72` |
| C5 | `6ed5257ee269ce20612188d019b7af44fb3b4c1fea0c080c29517438219f1385` | `ad4f2164d50e921f00b22d938ef86377fca4a642304f8acd1487a23c2bcda7c8` | `6bede11b183ca33d0ccc17a504293fd5fc64d58f1e3871e553acd4d6c38c8fe5` |

The installed development layout cannot accommodate secure bootloaders and their
signature sectors. The [factory-review layout](../security/README.md) moves only
pre-app metadata/table regions and adds encrypted NVS keys. Original app, assets
and coredump offsets/sizes remain; assets/coredump gain encryption flags. NVS
uses its separate encrypted storage scheme. Runtime encryption checks and secure
ROM mode are enforced; validation skips and insecure options fail the auditor.
Factory SDK code uses size optimization with active silent assertions, retaining
P4 peripheral `-O2`, C5 peripheral/Muse `-Og`, existing per-file policy and emulator
`-O2`. C5 memory settings and protocol v8 remain intact. P4 `.hb` trust stayed in
development mode because no production Ed25519 identity was supplied.

Validation completed locally:

- `test_security.py`: 11 config/layout/signature-space/failed-review tests pass.
- Both `secure_build.py verify` commands pass against actual SDK-produced images
  and tables, including checksums, SHA, chip IDs and signed-capacity reservations.
- Both matching SDK Pythons run `test_security_images.py` successfully. Official
  `espsecure` verifies actual app/bootloader signatures and public-key digests;
  wrong/private/undersized keys, payload/signature tampering, truncation, changed
  review content and output overwrite are rejected. Both actual signed images
  round-trip through XTS encryption/decryption at their intended addresses;
  wrong keys and addresses do not reproduce the plaintext.
- The ordinary paired build and complete offline regression suite pass through
  `package.py`. YAML/shell syntax, commitlint and whitespace checks pass. New CI
  steps are configured; a remote GitHub Actions run is not claimed.

Ordinary app-only candidate:
`integration/release/candidate_f30c4e873a2c_ed159f236e7e_19d7131d3761/`.
Its source report is `5c102eaf6d983aa8a27c93756eb90b5d1abf5799`, dirty for user
changes. Later CSV whitespace/documentation commits do not change executable
behavior. App sizes/margins remain unchanged from the #153 candidate:
P4 2,946,960 bytes / 2,160 free; C5 2,051,664 bytes / 45,488 free.

Normal P4 SHA-256: `f30c4e873a2c36ec3e61950053b467c25ff1776867f1c18f077f2b3e1c8bdd26`.
Normal C5 SHA-256: `ed159f236e7ec64fa3f23812f984eebed7b34c3cff4821eea4f83d9917087e39`.

`verify_package.py` and real-esptool recovery-image checks pass for this ordinary
candidate. It keeps the original development table/configuration and does not
contain factory bootloaders or security provisioning. The full host-runner
fingerprint remains `3b7ab188670d774171e88207e63b57a8b1b425838e3e3562bc30ad5c0747a4bc`.

No flash, serial device, eFuse, production key or installed filesystem was
accessed. Temporary host fixture keys alone exercised crypto. Factory metadata
export/reseed/reset, production identities/independent per-device key custody,
assets compatibility, paired first boot, hardware encryption/signature enforcement
and physical secure recovery remain installation/release prerequisites under
#157. All review deployment/provisioning/hardware gates stay false. The factory
layout is not an ordinary app-only update and existing plaintext NVS is not
migrated in place. No changes were pushed; user changes/provenance are preserved.


## Issue 157 paired RAM/compiler evidence and SD OTA packaging

The 2026-10-22 software checkpoint at code revision `5ed9e5052cd19da8287c9b62acbae3f972339328` passed the normal
P4 IDF v5.5.3 / C5 IDF v6.0.1 builds and the complete offline runner. The source
was dirty, including the user's existing ESP SCAN/guarded-updater work. All 47
initial worktree/provenance files were retained; the runner gained only the
release regression invocation for this change.

The 12 new release tests cover both SDK size formats, flash/LP exclusion,
internal RAM regression floors, malformed counts, effective compiler/LTO
ordering, missing/duplicate critical sources, candidate/evidence binding,
fabricated acceptance, missing evidence, correct paired SD paths, corruption,
symlinks, extra payloads, atomic failure handling and immutable repetition.
Existing package/security/recovery and firmware/ABI/OTA fault-injection tests
also passed. These are explicit host fixtures, not physical acceptance.

| Target | Application bytes | Original slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,946,960 | 2,949,120 | 2,160 | `fae6c29bddeec6dc852c45e910e8c1c92c9f86671e4ef9a0fd826a9405e778ab` |
| C5 | 2,051,664 | 2,097,152 | 45,488 | `340a2825c8aa7479f09ca37c6c8e687eaddc4d78ecb1515659d2cf578f5dc7ab` |

The source/compiler policies and application lengths remain unchanged from the
#152 normal-build checkpoint. Hashes differ because the rebuilt descriptors
include the newer source revision; byte-identical binaries are not claimed.

| Main internal region | Total bytes | Linked bytes | Linker headroom bytes | Minimum headroom bytes |
| --- | ---: | ---: | ---: | ---: |
| P4 DIRAM | 576,464 | 403,144 | 173,320 | 163,840 |
| C5 HP SRAM | 320,928 | 255,243 | 65,685 | 61,440 |

The matching SDK's actual size tool supplied these counts (P4 legacy JSON,
C5 JSON2 1.2). They include linked text/data in main internal RAM and are not
live heap, minimum heap, available DMA memory or stack measurements. Native
size reports and selected effective compiler policies are embedded in the
candidate manifest and bound to these build artifacts:

- P4 elf_sha256: `2c9acf357e7cb53274b4cfc4972032ddcfa961ba36695a71e08508da1e038ae9`
- P4 map_sha256: `5467bb04f9352d65783ac02a88223d61b4e6d2891633dbf65ec97390851d68b3`
- P4 sdkconfig_sha256: `0c4b61f6047777a83b2a87efadd031f319887d49a1921fcf3da39d2b1aad102f`
- P4 compile_commands_sha256: `9c6bb094358e47ff9dcc5261521652f0be493fb911f3571749361b6c01f10338`

- C5 elf_sha256: `36adff31d38a933df88a30303287a3acc0127d48e145f1d38686eda4b3ad0319`
- C5 map_sha256: `0a7165262e1320a91bfd24a7f55844816d8c61706906412c8ec29a5c48f2cf61`
- C5 sdkconfig_sha256: `42acb53bf0ca79e3d1f5d1ff246e64e573cd61cce58221d9d5288f4e9893aab2`
- C5 compile_commands_sha256: `aeb07da4aaf018f8a84412f51f35971a88a07655c13ce609cde6359f7f44ffcd`

Runner SHA-256: `e84ac2d85bb17660b55e5cafecefc31980cd0d0784fcc4cdd537ab7458df85e1`.

Verified app-only candidate:
`integration/release/candidate_fae6c29bddee_340a2825c8aa_9337e07a5118`.

Candidate manifest SHA-256: `9337e07a5118354944c00fb2167ebbec6ac2023895a95d2b52bf3a9dc66d1f0e`.

Verified SD OTA package:
`integration/release/ota_9337e07a5118354944c0`.

Both SD copies match their candidate application hashes. Preparing the identical
package again returned the same verified immutable directory. The real esptool
image regression passed checksum/digest/chip checks on the newly built pair;
its deliberately wrong-chip case emits an expected warning. Boot containers
and assets in that test are synthetic; no actual restoration or boot is inferred.
CI YAML and every shell step passed parsing/syntax checks. Local collection
exercised the same new memory/compiler gate that CI will run; no remote Actions
success is asserted before push.

Issue #157 is software-complete at the user's requested boundary. The
[release procedure](RELEASE.md) keeps production identities/key custody,
approved restoration/migration/assets, real paired OTA/rollback and runtime
performance/heap/stack acceptance as unfulfilled release gates. No manual
version change, public release, device access, security activation, flash/eFuse
write or push occurred.


## Issue 158 UART Backspace reproduction and editor correction

The user clarified the original “bash bug”: holding Backspace in the UART
console corrupts editable input. ESP-IDF v5.5.3's actual editor reproduced it
without a device: bytes read within 30 ms bypassed normal control handling and
were inserted as pasted text, including DEL. Its fast path also silently dropped
input when the cursor was in the middle of the line.

The generated P4 console source now limits that fast path to printable bytes
at the line end. BS/DEL, ANSI escapes and Ctrl keys reach the normal editor;
printable middle insertion uses ordinary insertion/redraw. SDK sources remain
unchanged. Generation requires this original SDK SHA-256:
`38753ab0df3140232ed65a9b412ba670497eaebe424a45098eaaebb2dc5c4fce`.

Actual compiled generated-source SHA-256:
`1129f072405bf3236065cd5536e4ee2ffef0a9e0af0a1ed3e483c43b48158f91`.

`test_console.py --idf <pinned-idf> --build firmware_p4/build_highboy` first
confirmed that the unmodified editor fails the rapid-DEL expected-result case.
With the fix, all **40** fast/slow real-editor cases passed ASan/UBSan: both
Backspace encodings, over-deletion, arrows, middle editing, Delete, ANSI/SS3
Home/End, Ctrl editing, long paste and 511-byte bounds. UART reads, clock,
terminal-query replies and output are explicit host fixtures. The test also
rejects an altered SDK source and checks that the real compilation database
selects exactly the generated corrected file. No actual UART or serial terminal
was opened. CI runs this separately in the P4 SDK build job; it is not silently
claimed as part of the SDK-free host runner.

The paired rebuild at `30ef9f85deaeecadfb9663babd92e4dce6561559` and complete offline runner passed, including
source/provenance, memory/compiler-policy, package/security/recovery, native
firmware/ABI and OTA regressions. The working tree was dirty with the user's
retained ESP SCAN/updater work. Application byte lengths and original slots
remain unchanged from the #157 checkpoint; hashes reflect rebuilt descriptors
and the corrected P4 editor, not byte-identical artifacts.

| Target | Application bytes | Original slot free bytes | SHA-256 |
| --- | ---: | ---: | --- |
| P4 | 2,946,960 | 2,160 | `22a6069de3b3c7e082507b09122a5f3ca1b715d5a5827d4db217bfccf6757972` |
| C5 | 2,051,664 | 45,488 | `0ee960c5e4c10c9f9923f83a1d31cb69956b1c870576f4fcfa5322a73c2f4ab9` |

- P4: 403,144 linked internal bytes; 173,320 linker headroom bytes.
- C5: 255,243 linked internal bytes; 65,685 linker headroom bytes.

These are static SDK linker measurements, not runtime heap/stack acceptance.
Runner SHA-256: `e84ac2d85bb17660b55e5cafecefc31980cd0d0784fcc4cdd537ab7458df85e1`.

Verified candidate:
`integration/release/candidate_22a6069de3b3_0ee960c5e4c1_2ed165544e64`.

Verified paired SD export:
`integration/release/ota_2ed165544e648c56013a`.

Candidate/SD verification passed. CI YAML and all shell steps parse; remote
Actions have not been run before push. The installed SDK retained its original
source hash. The reported console defect is software-complete under #158;
physical terminal/adapter behavior and asynchronous-log interference remain
unmeasured. The console is the existing ESP-IDF CLI; shell-language compatibility
was not added. Consumer restoration/production requirements remain documented
release gates, not completed device results. No firmware/eFuse write or push
was performed.


## Local ESP-IDF v6.1 migration — 2026-10-26

Both normal applications built with the locally installed ESP-IDF tag `v6.1`.
The original partition maps, paired SPI v8 contract and C5 memory overlay remain
in place. Build evidence passed the existing compiler-policy and RAM thresholds.

| Target | Application bytes | Original slot free bytes | Internal RAM headroom bytes |
| --- | ---: | ---: | ---: |
| P4 | 2,896,096 | 53,024 | 180,520 |
| C5 | 2,084,784 | 12,368 | 62,807 |

Both factory-security application builds and real image cryptography regressions
passed with fixture identities. These are build/test artifacts, not provisioned
production images. No flashing or eFuse writes were performed.

Local checks passed for package/release/security policy, SDK console editing
(40 cases), legacy DES/3DES (12 upstream vectors), real companion pairing,
JS/Lua engines and MeshCore key agreement with real libsodium. The offline
aggregate passed its other groups; its preservation check initially observed
concurrent source edits and passed separately after the correction ledger was
updated. The preserved-source suite covers 34 tests.

RAM figures are static linker headroom, not measured runtime heap. This record
does not assert hardware acceptance or a remote GitHub Actions result. The v6.1
crypto compatibility path uses pinned private SDK primitive headers and must be
revalidated on subsequent SDK upgrades.


## Public PSA migration — 2026-10-26

Active NFC/LoRa, companion and Muse pairing paths now use the public PSA
interface for the migrated primitives. Noise already selects its PSA backend;
DES/3DES remains isolated for legacy NFC. See [crypto](../crypto/README.md).

The final paired app builds and compiler/RAM evidence checks passed on IDF v6.1.
Partition layouts and SPI v8 remain unchanged. P4 uses software AES to avoid the
pinned SDK's missing PSA hardware CCM implementation; hardware timing and
throughput have not been measured.

| Target | Application bytes | Original slot free bytes | Internal RAM headroom bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,927,664 | 21,456 | 180,240 | `cdadead97ff6e65d4e082da45b1e08b6a738b5980b4f7a154800e77eaff292ef` |
| C5 | 2,085,536 | 11,616 | 62,927 | `420f67033ac27b0e13f703d577bde591bba4b526be64141343994798b6b93177` |

The complete offline runner passed after updating LoRa fixtures for explicit
crypto error returns. The real SDK software PSA test passed with ASan/UBSan:
SHA/HMAC/HKDF, AES ECB/CBC/CTR/CMAC/GCM/CCM, NFC chaining IVs, partial/empty
messages, tampered tags, RFC 7748, P-256 encoding/agreement and repeated volatile
key cleanup. Python cryptography supplies independent expected results. Host
companion interoperability retains its explicit OpenSSL/libsodium adapter and
OS/NVS/UI fakes; it is not presented as a hardware PSA test.

Both isolated factory-security builds and their real image signature/encryption
regressions passed with fixture identities. No device, eFuse, production signing
identity or remote GitHub Actions execution was used. CI now also runs the PSA
host test from the pinned SDK in its P4 build job.

## CI checkout corrections — 2026-10-26

Actions run `40274783654` exposed three test-environment defects after the PSA
migration. Both host compiler lanes failed because the MeshCore test assumed a
single-line function signature and the Lua provenance inventory referenced an
upstream `src/Makefile` excluded by the global Makefile ignore rule. The P4 job
compiled successfully but failed at the subsequent PSA test because its Python
interpreter lacked `cryptography`.

The MeshCore extractor now accepts whitespace between the return type and name.
The unchanged upstream Lua Makefile is included through a narrowly scoped ignore
exception, preserving its recorded SHA-256. The P4 PSA step explicitly installs
`tools/crypto/requirements.txt` using the same Python interpreter as the test.
These changes do not relax assertions or change firmware behavior.

Local validation includes the real SDK v6.1 software PSA vectors with ASan/UBSan,
Lua upstream Makefile hash verification, and CI YAML/shell syntax checks. Remote
GCC/Clang acceptance still requires a new Actions run; this macOS environment
has Clang but no native GCC installation.

The complete `tools/highboy/test.command` suite also passed with Clang from a
snapshot containing tracked files plus the proposed changes, without local
ignored source files. The snapshot used the already resolved managed cJSON
sources and native libsodium/OpenSSL installations. No firmware rebuild or
hardware operation was needed for these test-only corrections.
