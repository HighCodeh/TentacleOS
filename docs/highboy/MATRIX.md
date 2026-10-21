# Peripheral integration matrix

Initial checkpoint: firmware `555742ec`, ABI 1.10; follow-up records below supersede changed rows. “Implemented” means a native
software path exists; no row has completed physical acceptance on the paired
High Boy. HID layouts, haptics and HLE remain excluded by request.

| Peripheral / operation | Device UI | Signed `.hb` ABI | Companion | Remaining acceptance / code |
| --- | --- | --- | --- | --- |
| Wi-Fi discovery / connect / sessions | Native P4→C5 services | `wifi` table | Authenticated Wi-Fi command set | Paired scan/connect/session matrix and measured live throughput (#143) |
| BLE discovery / connect / HID mouse | Native P4→C5 services | `ble` table; mouse is not an ABI entry | Authenticated Bluetooth command set | Device interoperability; remote GATT discovery/read now has a bounded native explorer; physical acceptance remains (#156) |
| NFC A/B/F/V identification | Native reader screens | `nfc.poll`, JS/Lua `nfc.poll` | READ / STATUS / RECOVER | Real cards, antenna shutdown and transport tests (#148, #156) |
| NFC block write / emulation | Native services and UI | Unpublished optional pointers are NULL | Not implemented | Add explicit sessions/ownership and acceptance; discovery coverage does not imply writes |
| LF RFID EM4100/EM4102 read | Native LF worker | `rfid.read`, JS/Lua `rfid.read` | READ / STATUS / RECOVER | Physical field/capture validation; no write/clone ABI |
| Infrared capture / send / library | Native RMT and bundled/SD library | ABI 1.11 decoded receive/send; JS/Lua bindings | Native capture / transmit commands | Shared app-worker lease and retained cleanup implemented; raw/file/AC API and physical acceptance remain outside the published subset (#149, #156) |
| Sub-GHz receive / raw replay / spectrum | Native CC1101 | ABI 1.13 bounded receive/raw TX plus latest-capture observation; JS/Lua | Native handlers and streams | Asynchronous/file/spectrum app adapters and physical RX/TX/spectrum (#149, #156) |
| LoRa MeshCore / Meshtastic | Native mesh sessions | ABI 1.12 chat observation; JS/Lua | Native chat/configuration bridge | Clean protocol ownership/teardown and active-control adapter (#149); live mesh acceptance |
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

Issue #156 remains open: absent adapters and physical rows cannot be replaced
by this source matrix. Unpublished radio adapters and production provisioning still require implementation
or configuration; the physical matrix remains separate.

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
