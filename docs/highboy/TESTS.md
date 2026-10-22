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
The vendored source hashes and native screen routing are also checked.

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
the C5 memory overlay and original app slots are unchanged. Commitlint and
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
