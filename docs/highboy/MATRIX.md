# Peripheral integration matrix

Source checkpoint: firmware `555742ec`, ABI 1.10. “Implemented” means a native
software path exists; no row has completed physical acceptance on the paired
High Boy. HID layouts, haptics and HLE remain excluded by request.

| Peripheral / operation | Device UI | Signed `.hb` ABI | Companion | Remaining acceptance / code |
| --- | --- | --- | --- | --- |
| Wi-Fi discovery / connect / sessions | Native P4→C5 services | `wifi` table | Authenticated Wi-Fi command set | Paired scan/connect/session matrix and measured live throughput (#143) |
| BLE discovery / connect / HID mouse | Native P4→C5 services | `ble` table; mouse is not an ABI entry | Authenticated Bluetooth command set | Device interoperability; remote GATT discovery/read is not a complete explorer (#155) |
| NFC A/B/F/V identification | Native reader screens | `nfc.poll`, JS/Lua `nfc.poll` | READ / STATUS / RECOVER | Real cards, antenna shutdown and transport tests (#148, #156) |
| NFC block write / emulation | Native services and UI | Unpublished optional pointers are NULL | Not implemented | Add explicit sessions/ownership and acceptance; discovery coverage does not imply writes |
| LF RFID EM4100/EM4102 read | Native LF worker | `rfid.read`, JS/Lua `rfid.read` | READ / STATUS / RECOVER | Physical field/capture validation; no write/clone ABI |
| Infrared capture / send / library | Native RMT and bundled/SD library | Not published | Native capture / transmit commands | App adapter and cross-client ownership/teardown before physical matrix (#149, #156) |
| Sub-GHz receive / raw replay / spectrum | Native CC1101 | Not published | Native handlers and streams | App adapter and session teardown; physical RX/TX/spectrum (#149, #156) |
| LoRa MeshCore / Meshtastic | Native mesh sessions | Not published | Native chat/configuration bridge | Clean protocol ownership/teardown and app adapter (#149); live mesh acceptance |
| LoRa airtime pacing | Shared SX1262 transmit guard | Future adapters inherit the driver guard | Existing TX callers inherit the driver guard | Host timing/concurrency tests pass; physical airtime/regional acceptance pending (#145) |
| LoRa MQTT subscription | Explicitly unavailable in UI | Not published | No MQTT socket service | Implement actual network transport, subscribe, fragmentation and receive path on the network-owning C5 (#145, #155) |
| RNode / KISS | Legacy screen is receive-only radio lab | Not published | No usable P4 KISS endpoint | Legacy S3 UART rejected because GPIO43/44 are P4 SDMMC; implement a separate transport (#145) |
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
by this source matrix. MQTT, GATT, RNode, ephemeral pairing and production
provisioning are implementation work, not merely missing test checkmarks.
