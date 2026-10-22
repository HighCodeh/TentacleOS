# Peripheral integration matrix

Initial checkpoint: firmware `555742ec`, ABI 1.10; follow-up records below supersede changed rows. “Implemented” means a native
software path exists; no row has completed physical acceptance on the paired
High Boy. HID layouts, haptics and HLE remain excluded by request.

| Peripheral / operation | Device UI | Signed `.hb` ABI | Companion | Remaining acceptance / code |
| --- | --- | --- | --- | --- |
| Wi-Fi discovery / connect / sessions | Native P4→C5 services | `wifi` table | Authenticated Wi-Fi command set | Paired scan/connect/session matrix and measured live throughput (#143) |
| BLE discovery / connect / HID mouse | Native P4→C5 services | `ble` table; mouse is not an ABI entry | Authenticated Bluetooth command set | Device interoperability; remote GATT discovery/read now has a bounded native explorer; physical acceptance remains (#156) |
| NFC A/B/F/V identification | Native reader screens | `nfc.poll`, JS/Lua `nfc.poll` | READ / STATUS / RECOVER | Real cards, antenna shutdown and transport tests (#148, #156) |
| NFC Classic block access / emulation | Native services and UI | ABI 1.17 bounded Classic 1K Key A blocks; ABI 1.22 owned UID emulation; JS/Lua | Authenticated block read/write 04/05 | Task-owned emulation and retained failed-OFF cleanup implemented; four-byte Classic 1K scope; physical acceptance unverified |
| LF RFID EM4100/EM4102 read | Native LF worker | `rfid.read`, JS/Lua `rfid.read` | READ / STATUS / RECOVER | Physical field/capture validation; no write/clone ABI |
| Infrared capture / send / library | Native RMT and bundled/SD library | ABI 1.16 decoded/raw/file/AC; JS/Lua bindings | Native capture / transmit commands | Shared app-worker lease, retained symbol cleanup and bounded single-signal file replay implemented; physical acceptance remains (#156) |
| Sub-GHz receive / raw replay / spectrum | Native CC1101 | ABI 1.21 owned RX/spectrum sessions; raw/file/bounded adapters; JS/Lua | Native handlers and streams | Windowed async lifecycle implemented; physical RX/TX/spectrum and runtime acceptance (#156) |
| LoRa MeshCore / Meshtastic | Native mesh sessions | ABI 1.23 owned raw/mesh lifecycle; copied nodes/chat/text; JS/Lua | Native chat/configuration bridge | Raw PHY and complete owned mesh teardown implemented; live RF/mesh acceptance unverified |
| LoRa airtime pacing | Shared SX1262 transmit guard | Future adapters inherit the driver guard | Existing TX callers inherit the driver guard | Host timing/concurrency tests pass; physical airtime/regional acceptance pending (#156) |
| LoRa MQTT subscription | Native Meshtastic MQTT screen | Not published | No companion MQTT API; C5 owns the network client | Bounded SPI transport, confirmed subscription and chat reception implemented; real broker and paired-device acceptance pending (#156) |
| RNode / KISS | Legacy screen is receive-only radio lab | Not published | No usable P4 KISS endpoint | Decision under #145: unsupported on this P4 profile because GPIO43/44 are SDMMC; a future endpoint needs a separate transport design |
| Power / recovery | Native diagnostics / safe mode | No power-control ABI | Existing system diagnostics | Sleep/wake, current draw, watchdog/crash and recovery acceptance (#135, #153) |

## Evidence and limits

The RF ABI tests compile production wrappers and broker code with explicit
hardware/RTOS fixtures. They check capability denial, cancellation before and
after locking, typed records, A/B/F/V mapping, queue failure, concurrent kill
quiescence and wire/local routing separation. Real JS/Lua tests check both
bindings, permission rejection and refusal of an older ABI table. Existing
NFC/RFID driver, protocol-routing, CLI, IR library, Sub-GHz, Muse, USB and DOOM
tests remain in the offline suite.

The `rfread` SDK example compiles against the self-contained ABI and requests
only receive permission. It is an installation artifact for a later approved
running device, not evidence that a card was read. See [build/test evidence](TESTS.md)
and [field recording](FIELD.md).

The source/ABI matrix is complete at the user's requested code boundary for
#156. Physical acceptance remains unverified and is recorded separately in FIELD.md
and the device-specific issues; the user requested completion without waiting
for those tests. Interface coverage is recorded per peripheral and published
operation; it does not imply that every native protocol laboratory feature has
an ABI or companion command. The explicit P4 RNode decision (#145), HID layouts,
haptics and HLE exclusions remain unchanged. No device test is inferred from an
implemented row.

## MQTT follow-up

Meshtastic home now exposes the real [MQTT bridge](../mqtt/README.md): P4
codec/chat integration over a bounded SPI service, with C5-owned TCP/TLS and
confirmed subscriptions. It is a native UI feature; no new `.hb` or companion
MQTT API is published. Real broker and paired-device acceptance remain pending.

## Remote GATT follow-up

The [native GATT explorer](../gatt/README.md) replaces the reference-only catalog.
It scans, connects, discovers services/characteristics and reads permitted
attributes over C5, with cancellation, an expiring connection lease and bounded
snapshots. The authenticated USB companion can use the same Bluetooth commands;
BLE companion ownership prevents simultaneous central exploration on that chip.
No new `.hb` GATT table is published, and physical acceptance remains pending.

## Companion authentication follow-up

The [ephemeral pairing implementation](../host_link/pairing.md) now requires
X25519 transcript proofs and physical code confirmation for USB/BLE. The actual
clients verify reply MACs/counters. Real-crypto host tests and paired builds pass;
transport/button/entropy acceptance remains physical work under #156.


## ABI publication scope

#149 publishes self-contained radio types/versioned optional tables and supplies
build-local app-signing trust selection with production configuration gates.
ABI 1.22/1.23 complete the NFC/LoRa lifecycle adapters previously tracked under
#156. The unsupported P4 RNode entry remains NULL under the explicit #145 decision. The real production app identity was
explicitly deferred by the user; selecting it and establishing custody remain
requirements of the #157 release handoff. Neither that identity nor Secure Boot/
Flash Encryption (#152) is implied by a successful ABI test.
