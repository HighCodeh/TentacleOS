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

# P4 code-size checkpoint — issue #157

Measured on 2026-10-21 against the working tree based on `0085b5a3`, including
its pending ESP SCAN and guarded updater changes. Both comparison builds use
that same source tree; the changed P4 libsodium compile policy is the comparison
variable. Existing partitions, features, trust identities and vendor sources
are unchanged. This is build evidence, not runtime performance acceptance.

## Selected policy

P4 libsodium uses `-Os -flto=2 -ffat-lto-objects`, joining the existing final
`-flto-partition=one` link. Its `sodium/utils.c` additionally ends with
`-fno-lto`: secret wiping and constant-time utility calls remain opaque to the
whole-program optimizer. The generated compile commands and final ELF were
inspected; `sodium_memzero` and `sodium_memcmp` remain linked functions.
C5, radio-driver and emulator compilation policies are unchanged.

## Application size

The original P4 slot remains **2,949,120 bytes** (`0x2d0000`).

| Candidate | Application bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | --- |
| Baseline | 2,949,104 | 16 | `b768bf459d8538a077888836399e82fd82c4af0a81fab849ee6a8faf49fb4d39` |
| Selected | 2,938,800 | 10,320 | `a14aa59e25799b325185c8941b2b9e2ac4e27e26410235d650635d11ab5e78d3` |

The selected policy recovers **10,304 bytes**, leaving **10,320 bytes** free.
The exploratory full-LTO build was smaller still, but is not the selected policy:
the utility translation unit stays outside LTO to preserve its call boundary.
This margin permits further development; it does not guarantee that all missing
adapters will fit or that a release candidate is ready.

## Static internal RAM

The P4 linker reports the same DIRAM region size, 576,464 bytes:

| Candidate | Linked DIRAM bytes | Unallocated linker bytes |
| --- | ---: | ---: |
| Baseline | 402,222 | 174,242 |
| Selected | 402,074 | 174,390 |

The linked footprint falls by **148 bytes**. These are linker counts, not live
free heap or runtime stack headroom. Reproduce with IDF 5.5.3's
`python -m esp_idf_size --format json --target esp32p4 <candidate.map>`.
Keep each baseline binary/map before rebuilding the selected policy.

## Validation and remaining acceptance

The portable libsodium 1.0.22 host source was copied into a temporary build,
configured with `--disable-shared --disable-asm CFLAGS='-Os -flto'`, and its
utility object rebuilt with a final `-fno-lto`. All **101 tests passed**, with
zero skips or failures. Host compiler/LTO differs from the RISC-V firmware;
these tests do not establish target timing or side-channel properties.

The real companion C security and Python clients also passed interoperability,
RFC 7748, replay/expiry, approval and forged-response tests against that host
library with LTO enabled at the host link. Serial, UI and platform calls remain
explicit fixtures. The complete offline suite and pinned paired application
builds are recorded in [TESTS.md](TESTS.md#p4-portable-crypto-lto-checkpoint).

Issue #157 remains open for runtime performance, internal heap/stack acceptance
and the reviewed OTA/assets/recovery package. No hardware, flash, production key
or eFuse was accessed during this optimization.


## ABI 1.17 development and secure-size boundary

The 2026-10-22 pair after `53788e90` uses the existing optimization policies and
original partitions. P4 is 2,946,368 / 2,949,120 bytes (2,752 free); C5 remains
2,051,056 / 2,097,152 bytes (46,096 free). Current IR/NFC adapters fit, without
claiming additional space for all optional APIs. See [the current build record](TESTS.md#shared-irnfc-and-offline-securityrecovery-preparation-build-record).

The new isolated security resolver makes a separate baseline-only projection:
default 64 KiB secure padding followed by a 4 KiB signature sector would require
2,953,216 bytes on P4 and 2,101,248 on C5, exceeding each preserved slot by 4,096.
That is a signing/padding footprint, not a claim that removing only 4 KiB of code
is sufficient: reaching the preceding padding page can require a larger code
reduction. A real secure build may also change dependencies and image size.
No secure binary, partition enlargement or short-partition exception was selected.
The normal pair remains a development candidate; physical performance and
internal heap/stack acceptance are still unmeasured.


## ABI 1.19 raw capture and file replay checkpoint

The verified app-only pair at code revision `ff4574cf` uses the unchanged selected
compiler policy and original slots. P4 is **2,947,888 bytes**, leaving **1,232**;
C5 is **2,051,056 bytes**, leaving **46,096**. Shared raw snapshots, strict RAW
and decoded parsing and worker-owned file replay add 1,520 P4 bytes compared
with the ABI 1.17 checkpoint. No component/feature or partition was removed.

An experiment changing only the final LTO partition policy from `one` to
`balanced` increased P4 to `0x2d3220` (12,832 bytes beyond its slot), so that
change was rejected and the CMake file restored byte-for-byte. The full offline
suite, pinned paired builds and candidate verifier passed with the original
policy. See the [complete build record](TESTS.md#sub-ghz-shared-raw-and-file-replay-build-record).

The 1,232-byte development margin does not solve the secure-padding boundary or
reserve space for all missing session adapters. #157 remains open for that code
size work and its performance/release requirements.


## Compact Core and resource manager control code

With the ABI 1.19 tree held constant, compiling only the `Core` and
`resource_mgr` components with `-Os -flto=2 -ffat-lto-objects` reduces P4 from
2,947,888 to **2,945,424 bytes**, recovering **2,464 bytes** and leaving 3,696.
The final link remains `-flto-partition=one`. A separate experiment adding
`-fno-inline-functions-called-once` at the link produced no size improvement
and was removed.

The selected control-code policy leaves all driver, emulator, C5 and portable
crypto compilation settings unchanged. In particular, `sodium/utils.c` stays
opaque to LTO. Original OTA slots and features are preserved. These are linked
application sizes, not live heap/stack or hardware performance measurements.
The following LoRa node adapters consume part of this recovered margin; their
final paired build record supersedes the control-only measurement.


## ABI 1.20 LoRa node checkpoint

After the new copied node observations and receive-parser fixes, the verified
P4 is **2,946,864 bytes**, leaving **2,256** in its unchanged 2,949,120-byte slot.
C5 remains **2,051,056 bytes**, leaving **46,096**. Relative to the ABI 1.19
starting pair, this batch recovers a net **1,024 P4 bytes** while adding the node
adapters. Full tests, hashes and source status are recorded in
[TESTS.md](TESTS.md#lora-copied-nodes-and-bounded-receive-build-record).

This remains development margin. It does not cross the previously documented
secure-padding boundary or reserve all space needed for async/session adapters.
No driver/emulator/crypto implementation or partition map was changed to obtain
these measurements; runtime memory/performance acceptance remains separate.


## Peripheral control size policy for owned Sub-GHz sessions

The new session lifecycle requires more room than the ABI 1.20 development
margin. Selected P4 control sources use a final `-Os` without LTO: CC1101 lease,
RF frontend, charger register/telemetry/battery service, NFC core/public wrapper,
and button/input services. Their source implementations, pins, task priorities,
periods and hardware settings are unchanged. Peripheral SPI/FIFO/IRQ/HAL, LF pulse
timing, CC1101 transfers, audio, emulators, C5 and crypto policies are preserved.
No feature or partition is removed.

An RF ownership LTO trial still exceeded the original slot and was rejected. The selected
policy stays outside whole-program optimization for those driver objects. The
final paired validation record gives the source-matched control-only baseline
and selected application sizes. These are build measurements, not hardware
latency/stack/power acceptance.


## ABI 1.21 complete Sub-GHz session checkpoint

With executable source frozen at `5dda6710`, the prior control-source policy
produces a **2,950,272-byte P4**, overflowing the original slot by **1,152**.
The selected final `-Os` policy for the nine control sources produces
**2,948,592 bytes**, saves **1,680**, and leaves **528 bytes**. The comparison
changes only that Drivers CMake source policy; both builds use the same code.
The oversized baseline is comparison-only and was not packaged. C5 remains
**2,051,056 bytes**, with **46,096** free. See the
[full build and test record](TESTS.md#owned-sub-ghz-sessions-and-forced-teardown-build-record).

This includes all published Sub-GHz entries, JS/Lua session bindings, cancellation
on context teardown and the manager's confirmed-suspension cleanup ordering.
The 528-byte margin remains inadequate for further large adapters and the secure
signing/padding boundary. Runtime stack/heap/timing, NFC emulation and LoRa
raw/lifecycle work remain separate; no release readiness is implied.


## NFC/LoRa lifecycle compiler policy

The P4 control policy also covers the pinned SDK console, argtable3, NVS,
partition/VFS/wear-level control, app update and app-format components with
`-Os -flto=2 -ffat-lto-objects`. SDK implementations, features and original partitions are unchanged. C5, radio
timing, rasterizer/emulator compiler policies and the opaque sodium utility
object retain their prior settings. On the held initial NFC tree, adding only console/
argtable3 recovered 5,344 bytes (2,951,024 to 2,945,680). Later lifecycle code and
additional control compilation supersede those intermediate image sizes; final
paired sizes/hashes are recorded in TESTS.md. Host tests and successful builds
do not establish runtime storage/console/OTA latency or hardware performance.


## Issue 157 software completion checkpoint

The 2026-10-22 paired rebuild at `5ed9e505` retains the prior firmware/compiler
policies and original slots: P4 2,946,960 bytes (2,160 free), C5 2,051,664 bytes
(45,488 free). This batch adds reporting and regression gates; it does not claim
an additional firmware size or timing improvement.

The actual matching SDK reports P4 DIRAM 403,144 / 576,464 bytes (173,320 linker
headroom) and C5 HP SRAM 255,243 / 320,928 bytes (65,685 headroom). The reviewed
regression floors are 163,840 and 61,440 bytes respectively. Main internal RAM is
accounted independently from flash, PSRAM and LP RAM. Existing allocation/stack
policies remain unchanged; linker headroom is not measured runtime free heap.

The [new release procedure](RELEASE.md) binds these reports and selected critical
compiler/LTO policies to each candidate, enforces them in CI, and exports paired
SD OTA inputs. Full build/test/artifact evidence is in
[TESTS.md](TESTS.md#issue-157-paired-ramcompiler-evidence-and-sd-ota-packaging).
This supersedes earlier statements that #157 still lacks software packaging or
signed-image capacity evidence. Runtime performance, heap/stack, actual update/
rollback and production installation remain unmeasured release gates after
software closure. No release version, device firmware or eFuse was changed.
