# Firmware issue audit

Reviewed against `dev` source through `cee87c22`, with the pinned paired builds
and the offline host suite passing. HID layouts (#64), HLE and haptics are outside
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

## Still open

| Issue | Remaining work |
| --- | --- |
| [#135](https://github.com/HighCodeh/TentacleOS/issues/135) | Battery ADC diagnostics exist. Light-sleep is disabled; audit UART/USB and peripheral wake/locking before enabling and measuring it on hardware. |
| [#143](https://github.com/HighCodeh/TentacleOS/issues/143) | Companion Wi-Fi dispatch exists. Measure actual throughput and validate complete scan/connect/session flows on the paired device. |
| [#145](https://github.com/HighCodeh/TentacleOS/issues/145) | Shared driver-level airtime pacing now covers both meshes and all SX1262 TX callers, with host-tested cooldown/concurrency behavior. Implement MQTT transport/subscription/receive flow, RNode remains explicitly unavailable on P4: its inherited S3 UART GPIO43/44 conflict with SDMMC, and entry points now reject before hardware access. A separate usable KISS transport is still needed. The MQTT UI no longer claims a synthetic connection. |
| [#148](https://github.com/HighCodeh/TentacleOS/issues/148) | Physical NFC card test with recorded hardware, card types, read/write/emulation results and field shutdown. |
| [#149](https://github.com/HighCodeh/TentacleOS/issues/149) | Publish usable radio API tables with capability checks, resource ownership and teardown. Build-local public-key selection and production rejection of the shipped dev key are implemented and host-tested. A real production identity remains unprovisioned; the user requested configuration support first. Radio publication and teardown remain open. |
| [#151](https://github.com/HighCodeh/TentacleOS/issues/151) | Companion ephemeral ECDH and device-side pairing confirmation. Existing PSK authentication and separate Muse pairing do not meet this requirement. |
| [#152](https://github.com/HighCodeh/TentacleOS/issues/152) | Production Secure Boot v2 and Flash Encryption provisioning for both chips, including keys, recovery and irreversible eFuse review. Both remain disabled. |
| [#153](https://github.com/HighCodeh/TentacleOS/issues/153) | Existing boot-loop/safe-mode/WDT/coredump mechanisms need fault-injection and recovery acceptance on hardware. The boot-sound crash workaround remains. |
| [#155](https://github.com/HighCodeh/TentacleOS/issues/155) | MQTT is now honestly unavailable; implement its real transport. Dev Scripts now runs real JavaScript/Lua files separately from signed `.hb` apps and shares `tos_api_t` through per-run capability approval, resource ownership and cleanup; see [scripts](../scripts/README.md). GATT is explicitly a reference catalog and no longer invents read values; remote discovery/read integration remains. Native Wi-Fi connect, BLE mouse/companion and LoRa position paths exist but need device acceptance. |
| [#156](https://github.com/HighCodeh/TentacleOS/issues/156) | Complete and exercise the UI / `.hb` / companion peripheral matrix after the missing ABI and host-link paths are implemented. |
| [#157](https://github.com/HighCodeh/TentacleOS/issues/157) | Measure performance/internal RAM and prepare a reviewed OTA/asset/restore package. P4 code space is nearly exhausted; app-only builds are not a release candidate package. |
| [#158](https://github.com/HighCodeh/TentacleOS/issues/158) | Fixed a reproduced BSD/macOS SDK build bug (`stat -c%s` is GNU-only), with an isolated shell regression. The original unnamed Bash report remains unidentified: the user no longer recalls the command and the explanation is on another PC. All reviewed shell files pass syntax checks. Field/consumer documentation needs the approved deployment/recovery procedure. |

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
