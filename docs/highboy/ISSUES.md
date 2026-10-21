# Firmware issue audit

Initial audit against `dev` source through `cee87c22`; follow-up sections and
the linked validation records document later implementations. HID layouts (#64), HLE and haptics are outside
this work. No device was flashed or exercised.

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

## Still open

| Issue | Remaining work |
| --- | --- |
| [#135](https://github.com/HighCodeh/TentacleOS/issues/135) | Battery ADC diagnostics exist. Light-sleep is disabled; audit UART/USB and peripheral wake/locking before enabling and measuring it on hardware. |
| [#143](https://github.com/HighCodeh/TentacleOS/issues/143) | Companion Wi-Fi dispatch exists. Measure actual throughput and validate complete scan/connect/session flows on the paired device. |
| [#148](https://github.com/HighCodeh/TentacleOS/issues/148) | Physical NFC card test with recorded hardware, card types, read/write/emulation results and field shutdown. |
| [#149](https://github.com/HighCodeh/TentacleOS/issues/149) | NFC discovery and native LF RFID read tables are published in ABI 1.10, including JS/Lua bindings, stable-worker ownership, capability checks and force-kill quiescence. IR decoded receive/send is now published in ABI 1.11 with JS/Lua bindings and a shared worker lease. ABI 1.12 now publishes read-only Sub-GHz capture and LoRa chat observations, including JS/Lua and private cursors. ABI 1.13 adds bounded Sub-GHz capture/raw TX with shared JS/Lua bindings, a CC1101 lease and retained cleanup. Asynchronous/file/spectrum app control and active LoRa remain pending. Build-local public-key selection and production rejection of the shipped dev key are implemented and host-tested. A real production identity remains unprovisioned; the user requested configuration support first. Remaining radio publication and teardown remain open. |
| [#152](https://github.com/HighCodeh/TentacleOS/issues/152) | Production Secure Boot v2 and Flash Encryption provisioning for both chips, including keys, recovery and irreversible eFuse review. Both remain disabled. |
| [#153](https://github.com/HighCodeh/TentacleOS/issues/153) | Existing boot-loop/safe-mode/WDT/coredump mechanisms need fault-injection and recovery acceptance on hardware. The boot-sound crash workaround remains. |
| [#156](https://github.com/HighCodeh/TentacleOS/issues/156) | The [source matrix](MATRIX.md) now identifies implemented and missing UI / `.hb` / companion paths. Missing adapters and the physical cross-interface acceptance still need completion. |
| [#157](https://github.com/HighCodeh/TentacleOS/issues/157) | Measure performance/internal RAM and prepare a reviewed OTA/asset/restore package. P4 portable-crypto size optimization recovered 23,408 bytes before the RF ABI additions, with all 101 libsodium host tests passing; The 2026-10-21 LTO follow-up recovers another 10,304 P4 bytes on the current ESP SCAN candidate, with the utility translation unit excluded from LTO; 10,320 app-slot bytes remain. Device timing remains unmeasured and app-only builds are not a release candidate package. |
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
