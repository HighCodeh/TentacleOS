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

# Ethernet router service (C5)

`Service/ethernet_router` provides an esp-netif Ethernet uplink backed by bounded
SPI frame queues. C5 owns DHCP client, routing, hotspot DHCP/DNS and IPv4 NAPT.
P4 owns USB enumeration and power. Start requires Wi-Fi off and no active Wi-Fi
operation/session. Other Wi-Fi commands are busy while sharing owns the radio.
Sleep is blocked until teardown completes.

After Ethernet DHCP, the router rejects overlapping hotspot/uplink subnets,
selects Ethernet as the default route, advertises upstream DNS through hotspot
DHCP and starts the existing configured AP. It never substitutes an open SSID.
On stop/error it disables NAPT, stops the AP, restores DNS and previous Wi-Fi mode,
destroys the Ethernet netif and frees queues. The radio stays off as it was before
entry. An error is retained for the P4 to read after forwarding has stopped.
Static IP, IPv6 routing and modem uplinks are outside this implementation.

## SPI contract

Category `0x15` contains START, STOP, STATUS, legacy RX/TX fragments,
CAPABILITIES (`0x1506`) and EXCHANGE (`0x1507`). Both protocol-v2 headers
reserve the same IDs. P4 requires `SPI_ETHERNET_CAP_PACKET` before USB startup;
update both applications for this path. START carries a nonzero random session
token and adapter MAC. Other session operations reject stale tokens. Status
includes DHCP/sharing/error state, IP/mask/gateway and SSID.

EXCHANGE carries zero or one complete Ethernet frame in each direction, bounded
to 14–1514 bytes. The request body is `[u32 token][packet]`; the reply body is
`[u32 token][u8 RX status][packet]`. A zero-length packet means idle. The request
uses a CMD header with zero short length, followed by a u16 body length. The reply
uses a STREAM header with the same command ID and u16 body length. Both use the
existing 2048-byte inner frame / 2080-byte SPI envelope, including both CRCs.
Normal RESP errors remain valid. RX queue congestion is an explicit BUSY in the
reply body; TX still drains in that exchange, avoiding a bidirectional stall.
An exact request retransmission returns the transport cache without enqueueing
RX or dequeueing TX again. An ambiguous timeout never replays the packet under a
new transaction. The five-second master lease remains unchanged.

Legacy 240-byte fragments still accept exact token/sequence/offset/length checks.
A partial legacy packet prevents switching to EXCHANGE within that session;
restart the session before changing transfer modes.

Eight RX and eight TX entries plus two assembly buffers are in PSRAM, with internal
queue/mutex controls and an internal 6144-byte worker stack. esp-netif takes ownership
of a PSRAM RX copy through its free callback. TX makes a bounded temporary PSRAM
copy into the queue; no USB/SPI wait runs in the TCP/IP callback.

Build with C5 `CONFIG_LWIP_IP_FORWARD` and `CONFIG_LWIP_IPV4_NAPT` enabled.
The [P4 sharing document](../ethernet_share/README.md) describes deployment,
UI, power and the outstanding hardware validation checklist.
