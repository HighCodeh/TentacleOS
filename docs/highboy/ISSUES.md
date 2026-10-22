# Firmware issue audit

Initial audit against `dev` source through `cee87c22`; follow-up sections and
the linked validation records document later implementations. HID layouts (#64), HLE and haptics are outside
this work. Feature request #163 is also excluded by user request. No device was flashed or exercised.

## Implementation complete

| Issue | Evidence and boundary |
| --- | --- |
| [#139](https://github.com/HighCodeh/TentacleOS/issues/139), [#140](https://github.com/HighCodeh/TentacleOS/issues/140) | The RFID read screen initializes the native LF driver, starts its field, captures EM4100 data and stops the field from its owning worker. It replaces the old YS-UART manager path. Cancellation, reopen, allocation failures and result publication pass host tests. This does not implement RFID writing/emulation or prove electrical capture. |
| [#146](https://github.com/HighCodeh/TentacleOS/issues/146) | The NFC reader initializes the ST25R3916 through `highboy_nfc_init()` and starts `nfc_manager_start()`; initialization errors reach the UI. |
| [#147](https://github.com/HighCodeh/TentacleOS/issues/147), [#154](https://github.com/HighCodeh/TentacleOS/issues/154) | Read/write/emulation screens call native services. Legacy card emulation now opens the native MIFARE screen; each NFC screen stops its own worker. `nfc_sim` remains a library representation, not the source of native capture results. Hardware acceptance is tracked separately in #148. |
| [#142](https://github.com/HighCodeh/TentacleOS/issues/142) | The IR browser selects bundled `/assets/ir` collections or `/sdcard/ir/library`, with automatic fallback to bundled remotes when the SD library is absent/empty. The production parser loads 516 TV, 727 audio and 77 AC signals in host tests. Runtime assets must be installed; an app-only binary does not restore them. |
| [#144](https://github.com/HighCodeh/TentacleOS/issues/144) | P4 mouse input reaches C5 HID through `SPI_ID_BT_HID_SEND_MOUSE`. Companion BLE starts on demand and reports authenticated host-link state. Stale mock descriptions for native screens were corrected. Ephemeral pairing remains #151; device interoperability is not claimed. |
| [#150](https://github.com/HighCodeh/TentacleOS/issues/150) | Authenticated companion NFC-A/B/F/V identification and native LF EM4100 capture now have bounded READ/STATUS/RECOVER commands, task-owned antenna reservations, retained failed-OFF recovery and CLI tests. Haptics remains excluded by request. This supplies discovery/read commands; companion card writes/emulation and hardware/transport acceptance are not claimed. See [RF commands](../host_link/rf-commands.md). |
| [#155](https://github.com/HighCodeh/TentacleOS/issues/155) | Native Wi-Fi/Bluetooth paths, real Meshtastic MQTT/position, real JS/Lua Scripts and a remote GATT explorer now replace the audited mock paths. GATT scans, connects, discovers and reads through C5 with bounded snapshots, cancellation and lease expiry. Host tests and paired SDK builds pass; actual device/network interoperability remains #143/#156. |
| [#145](https://github.com/HighCodeh/TentacleOS/issues/145) | Shared SX1262 airtime pacing covers both meshes; real MQTT subscription/publication and bounded chat reception run through C5. The requested RNode decision is to leave the inherited UART endpoint unsupported on P4 because GPIO43/44 conflict with SDMMC; the menu is a receive-only lab. A future KISS endpoint requires a separate transport design. Physical acceptance remains #156. |
| [#151](https://github.com/HighCodeh/TentacleOS/issues/151) | Companion handshake v2 now uses fresh X25519, transcript-bound PSK proofs and physical token confirmation before signed FINISH/READY activates commands. Updated clients verify response MACs/replay counters; real-crypto C/Python interoperability, UI handler tests and paired builds pass. Physical USB/BLE/button acceptance remains #156. |

| [#149](https://github.com/HighCodeh/TentacleOS/issues/149) | Self-contained Wi-Fi/BLE/NFC/RFID/IR/Sub-GHz/LoRa types and versioned tables are published through ABI 1.15; supported operations use capability/cancellation/ownership adapters and JS/Lua bindings. Build-local public-key replacement and production rejection of missing/development keys are implemented. Real Ed25519 bundle verification is host-tested. The user explicitly deferred the real production identity and requested configuration support first. Optional unpublished adapters remain in #156; production identity/custody remains a prerequisite in [the release checklist](RELEASE.md#acceptance-outside-software-closure). See the scope reconciliation below. |

| [#156](https://github.com/HighCodeh/TentacleOS/issues/156) | The [source/interface matrix](MATRIX.md) is complete at the requested code boundary. ABI 1.22 supplies owned NFC UID emulation; ABI 1.23 supplies raw LoRa and complete owned mesh startup/teardown, shared with JS/Lua. Native ownership, callback/worker drainage and retained failed cleanup pass host tests and both pinned builds. The user requested completion without waiting for physical acceptance; device tests remain unverified, and P4 RNode retains the explicit #145 exclusion. |

| [#153](https://github.com/HighCodeh/TentacleOS/issues/153) | Checked P4/C5 monitor creation and WDT APIs now enter the configured panic/coredump path on failure. Healthy completed boots alone clear the RTC loop counter. Recovery preserves NVS, skips configuration/SD startup, gates UI/OTA on successful initialization, and reports reset failures without rebooting. Bounded internal-stack boot MP3/serialized I2S replaces the disabled crash guard. Production-code host fault injection and both pinned builds pass; physical fault injection, core-dump flash integrity and audio remain unverified at the user-requested code closure boundary. |

| [#152](https://github.com/HighCodeh/TentacleOS/issues/152) | Both pinned SDK lanes now build and audit real factory-security bootloaders/apps with Secure Boot v2 RSA-3072, release Flash Encryption and encrypted NVS/assets/coredump. Real signatures and full-image XTS round-trips pass with temporary fixture keys; app and bootloader signature sectors fit. The separate factory layout relocates metadata while preserving both app slots and assets. User-requested software closure does not certify a provisioned device; production identities, approved installation/migration and physical secure recovery remain [release prerequisites](RELEASE.md#acceptance-outside-software-closure). See [security](../security/README.md). |

| [#157](https://github.com/HighCodeh/TentacleOS/issues/157) | Existing optimization policies now have paired, hash-bound SDK internal-RAM reports and CI regression floors (160 KiB P4 / 60 KiB C5 linker headroom), plus guards on selected critical compiler/LTO policies. Verified SD OTA export supplies both existing update paths with immutable publication and corruption tests. Completed at the user-requested software boundary; runtime heap/stack/timing, actual paired OTA/rollback and production installation/security remain unmeasured release prerequisites in [RELEASE.md](RELEASE.md), not successful device results. |

## Still open

| Issue | Remaining work |
| --- | --- |
| [#135](https://github.com/HighCodeh/TentacleOS/issues/135) | Battery ADC diagnostics exist. PM initialization now acquires a guard before configuring sleep, serializes USB/client holds, preserves failed cleanup and retries failed input transitions. Enabled/disabled host fault-injection passes. Light-sleep remains disabled; UART/USB and peripheral wake/current must be verified on hardware before enabling. |
| [#143](https://github.com/HighCodeh/TentacleOS/issues/143) | Companion Wi-Fi dispatch exists. Measure actual throughput and validate complete scan/connect/session flows on the paired device. |
| [#148](https://github.com/HighCodeh/TentacleOS/issues/148) | Physical NFC card test with recorded hardware, card types, read/write/emulation results and field shutdown. |
| [#158](https://github.com/HighCodeh/TentacleOS/issues/158) | Fixed a reproduced BSD/macOS SDK build bug (`stat -c%s` is GNU-only), with an isolated shell regression. The original unnamed Bash report remains unidentified: the user no longer recalls the command and the explanation is on another PC. All reviewed shell files pass syntax checks. The [field guide](FIELD.md) documents current capabilities, bench evidence and the app-only boundary. A supported consumer installation procedure still needs the approved deployment/recovery package. |

## Validation of this correction batch

`bash tools/highboy/test.command` passed: configuration contracts, Sub-GHz,
RF/NFC/RFID ownership and cancellation, NFC screen teardown, IR source selection
and shipped catalogs, native Muse, 34 preservation/portable test cases, DOOM DMA
and USB MSC. Commitlint's own tests and the correction commit range passed.

Both pinned SDK builds passed with paired SPI v8, the C5 memory overlay and the
original partition sizes preserved:

| Target | Application bytes | Slot bytes | Free bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,946,384 | 2,949,120 | 2,736 | `beb721b4022a260121dc08b50a682b2572a33360342ac6d4447f2b32ec030cd0` |
| C5 | 2,016,048 | 2,097,152 | 81,104 | `041f2a8c12efc3bf2b523ecb74102602a88d4ddf89de7e09eee9958ea1f29a47` |

This is source/build evidence. Physical RF, BLE interoperability, current draw,
sleep/wake, networking throughput and recovery remain unverified.

## Script runtime update

The JavaScript/Lua implementation supersedes the earlier temporary Scripts →
Apps alias. Its languages, runtime limits and changed P4 optimization policy
are documented in [Scripts](../scripts/README.md). The paired build hashes above
belong to the earlier audit candidate, not to this interpreter update.

The [shared script ABI validation](TESTS.md#shared-script-abi-build-record)
supersedes the previous script-runtime hashes for the current candidate. The
published Wi-Fi/BLE, read-only filesystem and basic UI bindings are implemented;
unpublished radio tables and physical acceptance remain open.

## Companion RF capture update

The NFC/RFID implementation for #150 was validated after firmware commit
`5927bba0`; see [the paired build and test record](TESTS.md#companion-nfcrfid-capture-build-record).
Issue #150 was closed on 2026-10-19 for the agreed NFC/RFID scope; haptics
remains deferred. The #149 ABI/publication work, #151 ephemeral pairing,
MQTT/LoRa work and device/production acceptance issues remain open.

## App-signing configuration update

Firmware commit `1fa0778b` adds build-local public-key selection, production-mode
configuration gates and external SDK signing-key selection. The [paired build
record](TESTS.md#app-signing-trust-configuration-build-record) confirms unchanged
application sizes and restoration of the default development identity. No real
production key was provisioned, as requested. Issue #149 remains open.

## Shared airtime / RF ABI follow-up

The [current paired build record](TESTS.md#shared-airtime-and-native-rf-abi-build-record)
validates firmware `555742ec`: 21,536 P4 bytes free after the ABI additions,
101 passing libsodium host tests, and the complete offline regression suite.
The [matrix](MATRIX.md) shows which interfaces remain unavailable. No additional
issue is fully complete: remote GATT, ephemeral
pairing and IR/Sub-GHz/LoRa app adapters still require code; device-only tests,
production provisioning and the unidentified original Bash report remain open.

## Meshtastic MQTT follow-up

The [MQTT paired build record](TESTS.md#meshtastic-mqtt-bridge-build-record)
validates the C5 client, bounded SPI transport, reachable P4 screen, confirmed
subscription, encrypted publication and filtered inbound chat. The P4 retains
14,080 bytes and C5 55,936 bytes in their existing slots. Tests use explicit
network/transport/AES fakes; real broker and device acceptance remain pending.
No additional issue is closed: #145 still needs a usable P4 RNode endpoint and
#155 still needs remote GATT, alongside the other open requirements above.

## Remote GATT follow-up

The [GATT paired build record](TESTS.md#remote-gatt-build-record) validates
firmware `7aaae6b0`. This completes the remaining implementation work in #155.
The physical cross-interface matrix remains open under #156; discovery/read
fixtures do not prove actual BLE interoperability. Production provisioning,
ephemeral companion pairing and unpublished radio ABI adapters remain separate.

## LoRa scope completion

Issue #145 has an empty body/comments and asks for EU mesh duty-cycle pacing,
MQTT subscription and a RNode decision. The first two are implemented and
host/build validated. The recorded P4 decision is **unsupported legacy UART**
because of the SDMMC conflict. A usable KISS endpoint is a future transport
feature, not a completed capability of this firmware. This supersedes earlier
notes that kept #145 open for that additional endpoint. Actual regional airtime,
broker/device interoperability and cross-interface acceptance remain under #156.

## Ephemeral companion pairing completion

The [paired build record](TESTS.md#companion-ephemeral-pairing-build-record)
validates the #151 implementation. Every connection now requires the Companion
screen, a matching code and device OK; the old HELLO format is rejected. This is
a client compatibility change, with updated CLI/file-transfer clients included.
The frame envelope remains version 1, handshake payloads use version 2, and Muse
pairing is unchanged. Physical acceptance remains under #156.

## Native radio observation follow-up

The [ABI 1.12 build and static RAM record](TESTS.md#native-radio-observation-abi-build-record)
validates bounded Sub-GHz capture and LoRa chat observations in `.hb`, JS and Lua.
The Sub-GHz worker now retains DMA/resources until successful RMT cleanup; the
chat ring safely publishes its mutex on concurrent first use and handles cursor
wrap. The P4 has 4,384 bytes free in its original slot. Static linker RAM counts
are recorded separately from unmeasured runtime heap/performance.

GitHub was rechecked after #151 closure: nine issues remain in scope (#135,
#143, #148, #149, #152, #153, #156, #157, #158), plus excluded HID #64. Active
radio control and teardown still require code; physical tests, production
provisioning and the reviewed restore/consumer package are not complete.

## Bounded Sub-GHz follow-up

The [ABI 1.13 paired build record](TESTS.md#bounded-sub-ghz-abi-and-native-shutdown-build-record)
validates active, bounded capture/raw TX in signed apps and JS/Lua, CC1101
ownership/SPI error propagation and retained cleanup. Native TX and spectrum
workers now keep their reservations through shutdown. The final P4 candidate
has 8,336 bytes free in its original slot; hardware behavior is unverified.

This completes that bounded Sub-GHz implementation subset. Asynchronous/file/
spectrum app control, active LoRa, the production identity and physical matrix
still prevent closing #149/#156. The nine-issue balance is unchanged; HID #64,
HLE and haptics remain excluded. Pre-existing Wi-Fi and updater changes were
preserved outside this batch's commits.


## Queued LoRa text follow-up

The [ABI 1.14 build record](TESTS.md#queued-lora-mesh-text-build-record) validates
copied four-entry text submission to an already active native mesh from signed
apps and JS/Lua. Native UI and companion chat use the same queue and the selected
native poll task consumes it. Success means acceptance, not RF emission or
delivery; backend failure is logged without an outgoing chat record.

This completes the bounded mesh-text subset of #149. Raw LoRa/configuration,
mesh startup/teardown/node adapters, remaining asynchronous/file/spectrum
Sub-GHz calls, production identity and physical cross-interface acceptance are
still pending. None of the nine in-scope issues is closed by this batch.


## Bounded Sub-GHz spectrum follow-up

The [ABI 1.15 build record](TESTS.md#bounded-sub-ghz-spectrum-build-record)
validates complete 80-bin RSSI sweeps from signed apps and JS/Lua through the
retained RF worker. Band/span checks, capability/cancellation, SPI error
propagation and whole-result cleanup semantics are implemented and host-tested.
No device RF behavior is inferred from these fixtures.

This completes the bounded spectrum subset. Asynchronous/file Sub-GHz, raw LoRa,
mesh lifecycle/node adapters and production/physical requirements remain under
#149/#156. None of the nine in-scope issues is closed by this batch; HID, HLE and
haptics remain excluded.


## App ABI and trust scope reconciliation (#149)

The original GitHub issue was re-read on 2026-10-21: its title is
“.hb platform - 100 publish radio structs to the ABI + production trust-key”,
with an empty body and no comments. Both requested implementation surfaces are
present. The user separately chose **configuration support first**, deferring
the real production identity. No production key is generated or provisioned here.

Earlier follow-ups in this audit incorrectly made every optional radio table
entry and device acceptance a condition of #149. That expanded the scope beyond
publishing the ABI. The published contract explicitly supports NULL optional
pointers. Missing raw/file/async/session/node adapters remain honestly listed in
[MATRIX.md](MATRIX.md) under #156; no supported endpoint is inferred from a type
being declared. No RNode endpoint or clean mesh teardown is advertised.

The selected production trust store replaces the development key instead of
adding to it, and CMake rejects missing, malformed, zero or development keys in
production mode. The new offline test uses the real CMake generator, the real
`tos_hb_open`, libsodium verification and independent Python Ed25519 signing with
public RFC 8032 fixtures and the checked-in throwaway development seed. All 158 v2/v3 trust/tampering/truncation checks
pass, alongside the existing configuration and radio/script regression suites.
The actual identity, custody and signed release package remain #157; Secure Boot
and Flash Encryption remain the distinct #152 requirement. This resolves #149's
agreed implementation scope and supersedes its earlier “still open” notes.


## Recovery and power hardening follow-up

The next correction batch addresses #153 diagnostic accounting, boot-loop latching
and NVS/timer/summary failure paths, plus #135 PM initialization and USB/client hold
accounting. Real-module host fault-injection is included in the offline runner;
physical reset/watchdog/flash and sleep/wake/current acceptance remain pending.
Feature request #163 is excluded by user request, as are HID #64, HLE and haptics.
No diagnostic counters, user files, device flash, production identity or eFuses
were modified on a live device.


## OTA and app-only delivery follow-up

The P4 update UI was still a timed simulation; it now probes the actual SD image
and invokes native OTA through a pinned worker, with truthful version/progress/
error and deletion-safe snapshots. P4 updates are exclusive, check file reads and
partition preflight, and retain failed abort/power cleanup. P4/C5 now propagate
SDK state/confirmation errors instead of announcing a failed confirmation as
success. Host file/flash/LVGL fixtures validate the production paths.

The app-only packager previously rejected optimized-away names under the current
LTO build. It now records reviewed clone/inlined-console evidence, validates each
pair before atomic publication, and provides a standalone integrity/profile
verifier. Versioned metadata records the SDK/protocol/slots, dirty source,
selected public app-trust configuration and host-test runner fingerprint.
It does not certify production custody, physical results or full restoration.
These changes advance #153/#156/#157/#158; those acceptance requirements remain.

Only macOS debug/Bluetooth serial endpoints were visible when this batch checked
port names; no High Boy target port was available or opened. The remaining real
card (#148), throughput (#143), wake/current (#135), watchdog/recovery (#153),
cross-interface/performance (#156/#157), production identity/eFuse review (#152)
and missing original Bash reproduction/consumer restoration (#158) cannot be
reported as passed by substituting host fixtures. #163 and #64 remain excluded.


## Shared IR/NFC and offline production-preparation follow-up

ABI 1.16 implements every published IR entry: decoded RX/TX, bounded signed raw
capture/replay, single-signal SD-file replay and AC state encoding. The native
app table and JS/Lua bindings share capability/kill-gate checks and persistent
worker ownership. Symbol storage survives a timed-out RMT operation until
owned teardown succeeds. The native HEAT/DRY enum difference is translated.

ABI 1.17 adds Classic 1K Key A block read and verified data-block writes for
four-byte UIDs/SAK 08/88 through native apps, JS/Lua and authenticated companion
operations 04/05. Manufacturer/trailer writes are rejected; errors and failed
antenna/power cleanup are shared by STATUS/RECOVER and disconnect cleanup. The
two hand-maintained protocol-v8 headers reserve identical IDs in the same commit;
pending user Wi-Fi extensions remain outside those commits.

For #152, `tools/highboy/security.py` resolves/audits isolated production review
profiles using each actual pinned SDK. Secure Boot v2 RSA-3072 and release-mode
XTS-AES-128 are selected only in those review outputs; external signing, real
identities/custody, secure builds/slot checks and physical provisioning remain
required. Normal development defaults are unchanged. The baseline padding/
signature projection exceeds both original slots, so more size work is required
before a secure release; a configuration-resolution pass is not deployment proof.

For #157/#158, `tools/highboy/recovery.py` prepares/verifies private paired
recovery review bundles from complete capture-hashed backups and a checked
app-only candidate. It checks maps, chip/image integrity, OTA metadata, slot
limits, asset presence and copied content before atomic publication. It contains
no device-write action. Its synthetic tests and real esptool/built-app tests do
not prove a healthy assets filesystem, a bootable restored pair or physical
identity/security acceptance. See [the procedure](RECOVERY.md).

These are concrete software deliveries, not closure of all eight issues.
NFC emulation/session adapters, asynchronous/file Sub-GHz and raw/session/node
LoRa adapters remain code work under #156. A signed production build fitting the
original slots and a reviewed deployable restore/release package remain #157;
the original Bash reproduction is still missing under #158. Physical acceptance
and production identities remain distinct requirements. No device was accessed,
no production key was generated and no eFuse was changed by this batch.


## Sub-GHz shared raw and file replay follow-up

ABI 1.18 exposes the complete last successful bounded capture via `get_raw`,
including undecoded signals. ABI 1.19 exposes SD-file replay through the same
persistent RF worker used by raw transmission. Both are available to native
apps and JavaScript/Lua with explicit version/capability checks. RX snapshots
are globally bounded to one PSRAM buffer and survive failed captures/cleanup;
short output capacity returns an error without copying a prefix.

The UI and ABI now share the actual RAW/decoded capture parser and registry
encoders. Numeric overflow, malformed/duplicate metadata, oversized keys,
truncated pulse trains and unsupported encoders fail. App replay validates the
complete file, path, supported band, preset, pulse count and duration before
touching the radio, and retains in-flight RMT storage if teardown fails.
The native replay parser runs before stopping an existing receiver.

This implements the remaining file/raw-retrieval portion of #156. Its async
Sub-GHz session lifecycle, NFC emulation/session and LoRa raw/session/node
adapters still require code. These changes do not close #156 or certify the
physical cross-interface matrix. #157 still needs substantial size work for
secure signing and further adapters; no partition was enlarged. An experiment
with balanced LTO increased P4 beyond its slot and was fully reverted. No radio,
SD card, device flash or production identity was accessed by the host tests.


## LoRa copied node observations and receive parsing follow-up

ABI 1.20 implements the remaining LoRa node-count/get entries for native apps
and JavaScript/Lua. They share the native database-copy path now used by UI
node lists and companion sender lookup. Writers and snapshots use a separate
atomically published mutex; lock/allocation errors propagate through the new
API instead of returning borrowed entries. Node indices can change between
count/get. MeshCore contacts do not store RSSI/SNR and explicitly report zero;
Meshtastic uses its actual stored metrics and bounded name/ID.

The same change closes concrete receive-parser defects: MeshCore checks group,
direct and path ciphertext size against destination buffers before decrypting,
and direct-message ACK hashing uses a length-bounded scan. Meshtastic validates
remaining length before copying length-delimited fields and advancing fixed32
fields. Host ASan/UBSan cases cover oversized/non-terminated payloads and
malformed User protobufs. The actual databases are stressed with concurrent
readers/writers. Crypto/NVS/radio calls in those tests remain explicit fixtures.
MeshCore routing copies contacts and updates paths by public key under the
writer lock, preventing writes to an unrelated reused database slot.

For #157, compact `Core`/`resource_mgr` compilation recovers 2,464 bytes on the
held ABI 1.19 tree. With the new LoRa code, the final P4 build retains 2,256 free
bytes, a net 1,024-byte gain over the start of this batch. Drivers, emulators,
C5, crypto utility opacity, protocol v8 and original app slots are unchanged.
This is development margin, not completion of the secure signing-size work.

These changes advance #153/#156/#157. The remaining #156 code includes async
Sub-GHz lifecycle, NFC emulation/session ownership and LoRa raw/start/stop
adapters. No new app radio session is claimed by node observations. Hardware
acceptance and production release requirements are not replaced by these tests;
no issue was closed by this batch.


## Owned Sub-GHz sessions and safe forced app teardown follow-up

ABI 1.21 fills every published Sub-GHz function pointer. Native apps and JS/Lua
can start task-owned windowed RX or periodic complete spectrum sweeps, retune RX,
poll copied results and stop. The single persistent service worker uses the
existing RF backend/lease and performs no automatic SD writes. Empty windows
are distinguished from SPI/setup/cleanup timeouts. Generic ESP_FAIL remains a
negative poll error rather than becoming a successful count.

Binding/rebinding/unbinding cancels the originating task's session. Pending
requests, callbacks and buffers remain firmware-owned after app/script unload.
Failed teardown retains ownership and is retried before reuse. Foreign/stale
IDs fail; task-handle reuse cannot inherit a previous binding's session. Terminal
failures set resource_lost and successful startup clears it. This delivers the
remaining asynchronous Sub-GHz code under #156; reception is windowed with gaps,
not a claim of gap-free sampling or physical RF acceptance.

For #153, force-stop now confirms suspension and detaches context, sessions,
grants and app memory while the task identity is still allocated, then deletes
the task. The old delete-before-cleanup order could let an immediately reused
identity receive old teardown. Quiescence/suspension failures roll back without
freeing the live app; self-stop requests remain cooperative. Production manager
code is tested with delayed acknowledgement, failure and immediate reuse cases.

For #157, the source-matched control compilation comparison recovers 1,680 bytes,
leaving a verified 528-byte P4 margin with the new session and teardown code.
Original partitions, features, C5 overlay, radio transfer/IRQ/pulse policies and
crypto opacity remain preserved. The normal paired candidate passes the full
offline suite; the oversized comparison is deliberately rejected by the slot
check. This does not solve secure-signing size or runtime acceptance.

The remaining #156 code is NFC emulation/session ownership and LoRa raw/start/
stop adapters. Production identities, release/restoration work, original Bash
reproduction and physical acceptance for the other issues remain distinct.
No issue was closed and no device or production key was accessed in this batch.


## Issue 156 code completion

The remaining NFC emulation and LoRa raw/mesh lifecycle adapters are implemented
and published, with matching JavaScript/Lua bindings, canonical subsystem docs
and the updated peripheral matrix. NFC app and native listener workers reserve
the antenna across startup, use and retained failed cleanup. Every NFC target
startup SPI/IRQ error now propagates. LoRa raw, native mesh, receive lab and
console diagnostics share a single radio reservation. Private native owner tokens
prevent adoption or stale teardown of another stack. Mesh stop acknowledges poll/
TX workers, drains stream callbacks, confirms C5 transport stop, and waits for
IRQ completion and checked standby/HAL removal before releasing the radio.

The [paired validation record](TESTS.md#nfc-and-lora-owned-lifecycle-build-record)
records the exact candidate and host/hardware boundary. The user explicitly
requested finishing without waiting for physical testing. #156 is therefore
implementation-complete at that source/interface scope; no card, RF transmission,
mesh interoperability, electrical shutdown or live transport result is claimed.
RNode remains unsupported under #145, while HID layouts, haptics, HLE and feature
request #163 remain excluded. The other seven in-scope issues retain their own
code, production, reproduction and device requirements. No firmware was pushed
or flashed as part of this completion.


## Issue 153 operational code completion

Both monitors check task creation, watchdog subscription and every feed. Sustained
critical stack/heap or missing first/subsequent UI progress enters the configured
panic handler rather than a clean software reset, preserving diagnostic context
and abnormal-reset classification. The stack watch uses task handle plus number,
removes obsolete entries before reuse, and escalates a full critical-task table.
P4 stability requires completed kernel initialization, successful required stages,
a healthy monitor cycle and the elapsed stable window; the timer alone cannot
hide a stalled boot. The current recovery decision remains latched until reset.

P4 recovery no longer erases NVS on initialization failure, seeds/loads config
before deciding recovery, calls UI after failed LVGL initialization, or confirms
a pending OTA from recovery. Headless recovery supervises WDT/heap/stacks without
calling LVGL alerts/cache. Explicit factory reset mounts SD on request, preserves
assets/C5 firmware, yields during deletion, and propagates filesystem/NVS failures.
The recovery UI checks worker creation and only reboots after a successful reset.

The old unconditional boot-sound disable is removed. Normal boot optionally runs
a single guarded internal-stack worker with bounded file size, decoding time and
output duration, checked allocations and monotonic decoder progress. It plays
owned mono PCM through the serialized I2S path, with volume and short-write/drain
handling; recovery and low-heap/missing-file cases skip the optional sound.
The [validation record](TESTS.md#operational-recovery-and-bounded-boot-audio-build-record)
distinguishes production control code from fake scheduler/HAL/decoder behavior.

This completes #153 at the requested software scope without waiting for physical
acceptance. No actual watchdog firing, device crash reproduction, Helix execution,
flash core-dump capture/decoding, cache behavior or audible playback is certified.
No device was accessed or flashed, and no changes were pushed. The six other
in-scope issues keep their own remaining requirements; the feature request, HID
layouts, HLE and haptics remain excluded.


## Issue 152 software completion

Secure Boot v2 RSA-3072, signed boot/update validation, release XTS-AES-128,
runtime encryption checks and secure ROM download mode are resolved and compiled
for P4 v5.5.3 and C5 v6.0.1. New isolated factory profiles select encrypted NVS
with a flash-protected key partition and encrypted assets/coredump. Per-device
keys are not supplied or generated by the preparation tools. Host regression
fixtures generate temporary identities only and never select firmware trust.

Actual secure bootloaders cannot fit the installed 24 KiB region with their
signature sectors. The explicit factory-review layout moves the table/NVS/OTA/
PHY metadata into the available pre-app region and adds encrypted NVS keys.
Original app, assets and coredump offsets/sizes remain unchanged. The development
profiles, updater, partition files, protocol v8 and C5 memory overlay are kept.
Existing plaintext NVS is not migrated in place: installation requires reviewed
settings export/reseed or reset and both compatible signed images/assets.

The new builder uses a fresh output outside the checkout, audits real image
chip/checksum/SHA/config/table data and reserves the actual 4 KiB signature sector
in both app and bootloader capacities. It rejects failed/incomplete reviews.
A separate verifier consumes external signed images plus a public RSA PEM,
rejects private/foreign keys or altered/truncated inputs, records the actual
Secure Boot public-key digest and publishes complete reviews atomically.
Production identity/custody, deployment, eFuse and hardware gates remain false.

The [validation record](TESTS.md#paired-factory-security-build-and-crypto-record)
records real builds, full-image RSA/XTS tests, ordinary paired builds and the
complete offline regression suite. CI now runs the factory builds and actual
image crypto tests in the corresponding SDK matrix lanes; remote CI execution
is not claimed by local checks. This completes #152 at the user's requested
software boundary. Actual device provisioning, approved migration/installation,
production keys and physical secure recovery remain release prerequisites under
#157. No device was accessed, no eFuse was written and no changes were pushed.


## Issue 157 software closure

Current paired builds and the complete offline suite pass with linked memory/
compiler evidence in each candidate. The static main-internal-RAM gate retains
at least 160 KiB linker headroom on P4 and 60 KiB on C5; selected radio/audio/
render/emulator and opaque-crypto compilation policies are checked without
changing them. Immutable SD OTA export verifies both existing target paths,
artifact binding and exact copied bytes. See the [build record](TESTS.md#issue-157-paired-ramcompiler-evidence-and-sd-ota-packaging)
and [release procedure](RELEASE.md).

The user requested issue closure without waiting for device validation. Closing
#157 therefore records software completion, not v1.0-rc publication or production
approval. Runtime measurements, paired physical OTA/rollback, keys/custody and
approved assets/restoration/security migration remain release prerequisites;
older issue references to #157 do not assert those prerequisites were fulfilled.
No manual version change, physical write or push was performed.
