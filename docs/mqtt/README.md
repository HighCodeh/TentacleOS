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

# Meshtastic MQTT bridge

The P4 owns Meshtastic packet encoding and the chat inbox. The C5 owns the
MQTT client and TCP/TLS connection over its existing Wi-Fi station. The client
starts only when requested from **LoRa → Meshtastic → MQTT**. Start Meshtastic
and connect the C5 station first; this screen does not change Wi-Fi credentials
or start an access point. MeshCore is not supported by this adapter.

Enter a broker URI (`mqtts://hostname:8883` or `mqtt://hostname:1883`), optional
username and password, then Connect. TLS uses the C5 certificate bundle with
hostname verification. Plain MQTT provides no transport encryption. Embedded
credentials in the URI are rejected. The form stays in RAM across screen visits;
credentials are not written to NVS or logs. Disconnect is asynchronous and the
worker retries an unacknowledged stop. Leaving the screen keeps the requested
bridge running. Restart MQTT after changing the primary channel or region:
those settings are snapshotted at Connect.

## Packet contract

The primary channel name, PSK and hash come from the active Meshtastic stack.
The MAC-derived gateway ID is `!xxxxxxxx`. The bridge subscribes to
`msh/<region>/2/e/<channel>/#` and publishes to
`msh/<region>/2/e/<channel>/<gateway>`, with QoS 0 and no retained publish.
Channel names containing `/`, `+` or `#` are rejected.

The wire payload is the official Meshtastic
[ServiceEnvelope](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mqtt.proto):
packet field 1, channel ID field 2 and gateway ID field 3. Its embedded
[MeshPacket](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto)
uses encrypted payload **field 5**. Accepted local radio sends and radio receives
are exported as encrypted envelopes; own echoes and repeated sender/packet IDs
are filtered through bounded caches. Radio enqueue success does not prove an
on-air transmission or delivery.

Inbound messages must match the configured channel name/hash and be addressed
to this node or broadcast. The P4 decrypts with the snapshotted channel PSK and
accepts bounded TEXT_MESSAGE_APP data into the chat ring, labeled `mqtt:!<node>`.
Other gateways are allowed; gateway IDs and channel encryption do not provide
sender authentication. MQTT does not dispatch Admin, remote hardware commands,
or transmit received packets over the radio. Compressed text, JSON, PKI direct
messages and alternate channels are not implemented.

## SPI and resource bounds

Both protocol-v8 headers reserve category `0x14`. Commands are CONFIG `01`, START
`02`, STOP `03`, STATUS `04`, PUBLISH `05`, RECEIVE `06`, ACK `07`.

- CONFIG: field byte followed by bytes without a NUL terminator. Fields 1–5 are
  URI, username, password, subscription filter and publish topic. Capacities
  including terminators: URI 128, each credential 64, each topic 96 bytes.
  Configuration is rejected while a client exists.
- STATUS: 13 bytes: flags byte followed by little-endian u32 received, published
  and dropped counts. RUNNING `01`, CONNECTED `02`, SUBSCRIBED `04`, ERROR `08`.
  The P4 reports linked only after CONNACK and a successful matching QoS-0 SUBACK;
  network/subscription errors clear readiness. Reconnection belongs to ESP-MQTT.
- PUBLISH: sequence u32, offset u16, total u16, then at most 246 data bytes.
  All integers are little-endian. A complete packet is at most 512 bytes;
  contiguous assembly and duplicate chunks are checked before publication.
- RECEIVE: sequence u32 and offset u16; zero/zero peeks the head. The response
  repeats the eight-byte chunk header and data. An incomplete read does not pop
  the message. ACK(sequence u32) removes only the current matching head.

Each side has four bounded queue slots. A full queue drops the new message;
there is no disk backlog or delivery guarantee. TX counters mean MQTT-library
acceptance, not broker or subscriber acknowledgement. RX counters count complete
C5 envelopes before P4 filtering. P4 work runs on the radio core at service-low
priority; ESP-MQTT is also explicitly pinned to core 0. Disconnect flushes queued
traffic and clears the backend password; the UI form remains available for edits.
The persistent P4 worker blocks when stopped. START/STOP and SPI operations stay
outside the LVGL thread.

ESP-MQTT is fetched by the Component Manager from the Git revision declared in
`firmware_c5/main/idf_component.yml`, retaining its upstream Apache-2.0 license. P4 has no direct MQTT socket dependency. Regression tests
exercise bridge and codec behavior without requiring a per-file hash inventory.

## Validation and limits

`python3 tools/mqtt/test_mqtt.py` compiles the actual codec, C5 service and P4
adapter with ASan/UBSan. Tests cover official wire vectors, 20,000 malformed
inputs, allocation/start failures, subscription acknowledgement, fragmentation,
ACK/overflow, duplicate publication, reconnect state and canceled/lost START
cleanup. RTOS, SPI transport and MQTT networking are explicit host fakes. AES is
also a fake checking the key size/nonce layout; these tests do not prove crypto
interoperability. The firmware uses mbedTLS AES-CTR.

Actual broker/TLS interoperability, Wi-Fi reconnects, shared-chip load and radio
behavior require the paired hardware under #156. The #145 RNode decision is to
keep the conflicting inherited P4 UART transport unsupported; see [LoRa](../LoRa/README.md#rnode-transport-decision-for-high-boy-p4). Remote GATT is now implemented
separately; see [GATT](../gatt/README.md).
