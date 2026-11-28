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

# RogueMaster Sub-GHz codecs

The P4 uses the Sub-GHz protocol catalog from
[RogueMaster](https://github.com/RogueMaster/flipperzero-firmware-wPlugins/tree/4670a4cbe84a6e540eaaec5f9f943bfa8fee048a/lib/subghz),
pinned at `4670a4cbe84a6e540eaaec5f9f943bfa8fee048a`. Its 69 registry entries
are represented by 68 imported codecs and Highboy's native streaming RAW implementation.
BinRAW supports file replay and explicitly requested offline burst classification;
it does not replace the native RAW fallback. Offline classification feeds synthetic
start/end RSSI events solely to delimit the supplied pulse buffer, not to estimate
received signal strength.

## Credits

The imported Sub-GHz protocol encoders and decoders are credited to
**RogueMaster and the contributors to Flipper Zero firmware**, including the
original authors credited in the individual source files. Thank you to the
RogueMaster project for maintaining and extending this protocol catalog.

Upstream project: [RogueMaster / flipperzero-firmware-wPlugins](https://github.com/RogueMaster/flipperzero-firmware-wPlugins).
The imported code is distributed under the **GNU General Public License v3.0**;
the [upstream license](../../firmware_p4/components/Service/flipper_subghz/upstream/LICENSE)
and original source notices are preserved. Highboy's contribution is the ESP-IDF
platform adaptation and integration with its radio, storage and UI; the imported
protocol implementations are not presented as original Highboy work.

## Integration and license

This is an explicit local-source exception: upstream depends on Furi, FlipperFormat,
STM32 crypto, storage and allocation semantics that are unavailable in ESP-IDF.
The `Service/flipper_subghz` component supplies those narrow adapters. It does not
access CC1101, RMT, LVGL or the SPI bridge. Original source bytes and GPL license
are retained under `upstream/`; `upstream_manifest.json` pins every imported file.
Private compatibility headers are not exported to other components.

To update, select a reviewed upstream commit, replace the selected codec/support
sources and headers, update the source manifest, CMake source list and both catalog
adapters, then run the host tests and rebuild P4. Do not update the upstream files
through formatting hooks or edit generated dependency sources.

## Memory and lifecycle

Each codec operation holds a process-wide atomic guard and a temporary 512 KiB
PSRAM arena. Only one decoder is instantiated at a time; its workspace is reclaimed
before trying the next decoder. No internal-RAM fallback is used. The arena is
released on success, malformed input, allocation failure and upstream assertion.
Large input processing yields periodically to FreeRTOS. The radio worker still owns
capture cancellation, RX shutdown and transmission. A competing codec request
returns `ESP_ERR_INVALID_STATE` rather than racing upstream global button/counter state.

Decoded results retain up to 1024 bytes of complete protocol fields and a 96-byte
key string. Oversized serialization is rejected, allowing native Read to retain RAW
instead of saving truncated protocol data. File encoding accepts up to 16 KiB of
protocol text. RAW files use the independent streaming reader and are not limited
by that text buffer. A failed encoder publishes zero valid output pulses.

`flipper_subghz_describe()` deserializes a capture into the upstream decoder and
uses its `get_string` formatter inside the same guarded PSRAM workspace. It does
not invoke the encoder or transmit. The native replay owns a separate optional
1 KiB PSRAM description until freed; unsupported/oversized descriptions fall back
to basic metadata without blocking replay.

## Files, assets and capabilities

RCSwitch and decorated legacy protocol names are no longer registered. Files use
exact upstream names, complete Key bytes and protocol-specific fields such as
Data, Seed, Manufacture, TE and Guard_time. The existing Highboy frequency/preset
and custom Radio metadata remain supported. Hardware capabilities, antenna paths
and regional TX restrictions are not changed by a codec name.

The keystore accepts both `hex:type:name` rows and Highboy's existing
Manufacturer / Key (Hex) / Type asset format. It reads SD first, then bundled flash
assets. Importing a codec does not provide missing manufacturer secrets or seeds.
Nice FloR-S and Alutech additionally require appropriate plaintext binary lookup
tables at `/sdcard/subghz/assets/nice_flor_s` and
`/sdcard/subghz/assets/alutech_at_4n`. Encrypted Flipper device-bound table files are
rejected, not interpreted as lookup data. These optional tables are not installed
by an application-only flash. Protocols remain subject to their upstream
receive/transmit and key/table requirements.

Native Send encodes a loaded file once and repeats that prepared waveform while
held. Upstream rolling encoders may advance a code during preparation; persistent
rolling-counter updates across separate loads are not currently provided by the
native replay layer. Do not assume generic rolling-code remote pairing or replay
acceptance from codec availability alone.

## Catalog

The table records imported vtable capabilities, not physical interoperability.

| Protocol | Decoder | Encoder |
| --- | --- | --- |
| GateTX | Yes | Yes |
| KeeLoq | Yes | Yes |
| Nice FLO | Yes | Yes |
| CAME | Yes | Yes |
| Prastel | Yes | Yes |
| Faac SLH | Yes | Yes |
| Nice FloR-S | Yes | Yes |
| CAME TWEE | Yes | Yes |
| CAME Atomo | Yes | Yes |
| Nero Sketch | Yes | Yes |
| iDo 117/111 | Yes | No |
| Hormann HSM | Yes | Yes |
| Nero Radio | Yes | Yes |
| Somfy Telis | Yes | Yes |
| Somfy Keytis | Yes | Yes |
| Princeton | Yes | Yes |
| Linear | Yes | Yes |
| Security+ 2.0 | Yes | Yes |
| Security+ 1.0 | Yes | Yes |
| MegaCode | Yes | Yes |
| Holtek | Yes | Yes |
| Cham_Code | Yes | Yes |
| Power Smart | Yes | Yes |
| Marantec | Yes | Yes |
| BETT | Yes | Yes |
| Doitrand | Yes | Yes |
| Phoenix_V2 | Yes | Yes |
| Honeywell Sec | Yes | Yes |
| Honeywell | Yes | Yes |
| Magellan | Yes | Yes |
| Intertechno_V3 | Yes | Yes |
| Clemsa | Yes | Yes |
| Ansonic | Yes | Yes |
| SMC5326 | Yes | Yes |
| Holtek_HT12X | Yes | Yes |
| LinearDelta3 | Yes | Yes |
| Dooya | Yes | Yes |
| Alutech AT-4N | Yes | Yes |
| KingGates Stylo4k | Yes | Yes |
| BinRAW | Yes | Yes |
| Mastercode | Yes | Yes |
| X10 | Yes | No |
| Hormann BiSecur | Yes | Yes |
| Legrand | Yes | Yes |
| Dickert_MAHS | Yes | Yes |
| GangQi | Yes | Yes |
| Marantec24 | Yes | Yes |
| Hollarm | Yes | Yes |
| Hay21 | Yes | Yes |
| Revers_RB2 | Yes | Yes |
| Feron | Yes | Yes |
| Roger | Yes | Yes |
| Elplast | Yes | Yes |
| Treadmill37 | Yes | Yes |
| Beninca ARC | Yes | Yes |
| Jarolift | Yes | Yes |
| Ditec GOL4 | Yes | Yes |
| Doorbell32 | Yes | Yes |
| KeyFinder | Yes | Yes |
| KeyFinder2 | Yes | Yes |
| Nord ICE | Yes | Yes |
| Allstar Firefly | Yes | Yes |
| Telcoma/Cardin EDGE | Yes | Yes |
| Cardin S508 | Yes | No |
| Freezer | Yes | Yes |
| Faac TM | Yes | Yes |
| Proteco | Yes | Yes |
| BelKorn | Yes | Yes |

## Validation

`python3 tools/highboy/test_subghz.py` compiles the real adapters, codecs and native
serializer/replay with host hardware fakes and sanitizers. OpenSSL supplies the host
AES primitive (`OPENSSL_ROOT` selects its installation); ESP-IDF uses PSA Crypto.
These tests do not transmit RF or establish range, modulation tolerance, complete
manufacturer-key coverage or hardware equivalence to Flipper.

Host validation for this migration passed: exact 68-entry name/capability mapping,
independent Princeton timing/repeat vectors, a 44-bit Hormann round-trip, 176-bit
BiSecur file replay, malformed fields, noisy bursts, both keystore text formats,
AES known-answer vectors, arena-exhaustion recovery and native RAW/ownership/PTT
regressions. The supplied `RAWam650.sub` replay retained all 55 signed pulses,
116957 microseconds and its 433.92 MHz preset metadata. This is a file-level check,
not confirmation that those captured pulses decode as Princeton or work over RF.
