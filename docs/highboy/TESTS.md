# Offline preservation and Muse regressions

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

The C5 configuration check first applies `sdkconfig.defaults` followed by
`sdkconfig.baseline`, then verifies the effective
`firmware_c5/build_highboy/sdkconfig` when present. A missing generated
configuration is reported as pending, never as a successful build validation.
The build wrapper separately invokes `check_contracts.py --require-built <target>`
after configuration and after compilation, requiring the generated configuration
and enforcing the shared `C5_MEMORY` policy in the C5 build job itself.
`test_contracts.py` covers missing outputs, missing/incorrect/disabled C5 settings,
overlay overrides, valid configurations and prohibited security changes using
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
`firmware_c5/components/Service/highboy_muse` and
`firmware_p4/components/Service/highboy_muse`, using strict C11 warnings and
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
tests use canonical vendored cJSON.

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
No dependency is downloaded. `--skip-host` explicitly runs only the
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
