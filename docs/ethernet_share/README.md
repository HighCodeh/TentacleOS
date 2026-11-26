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

# Ethernet sharing service (P4)

Open **Wi-Fi → Hotspot → Share USB Ethernet**. Turn the Wi-Fi radio off before
entering using Connections or the Home/main-menu quick switch. Opening Wi-Fi
and Hotspot menus does not turn the radio back on or change its saved preference.
Before USB startup, the service applies the same firmware preset displayed by
Hotspot: `HighBoy_AP`, open (empty password), gateway `192.168.4.1`, eight clients.
The shared definition is `Service/wifi/include/wifi_hotspot_preset.h`; both the
normal hotspot and Ethernet sharing consume its SSID/password/IP/client limit.
It uses the existing checked AP configuration API, saving the preset on C5 while
keeping Wi-Fi off. Failure aborts setup and identifies the Hotspot preset stage.
The C5 reserves Wi-Fi for this session and starts that hotspot only after Ethernet
DHCP succeeds. P4 Connections settings are not rewritten; their normal boot sync
can restore their saved configuration, and entering Ethernet applies its preset again.
The screen reports adapter/link wait, DHCP, Ethernet IP, traffic counters and errors.
BACK stops the session and returns to Hotspot; OK retries after an error.
USB enumeration has a 20-second deadline after enabling power. Waiting for a USB
adapter and waiting for an Ethernet cable/link are distinct states. If internal
heap falls below 10 KiB, sharing stops with a visible memory error and releases
its resources rather than waiting for the global low-memory restart. USB
recognition timeout is also shown as an error; link wait begins only after the
CDC-NCM adapter is configured.
Errors retain the failing stage (USB ownership, Type-C, routing, power, adapter,
C5 radio reservation, DHCP or forwarding) and its ESP error code on screen and
in the terminal. Wi-Fi-off guidance applies to C5 radio reservation failures;
it is not a diagnosis for USB power or adapter failures. Stage reporting does
not relax any power guard. Hardware observations are recorded below.

Disconnect the computer/charger when prompted and connect the CDC-NCM adapter
to the Highboy USB-C port, then connect its Ethernet cable to a DHCP network.
The adapter's upstream network must differ from the configured hotspot subnet.
IPv4 Internet access depends on that network providing a working gateway and DNS;
“Sharing Ethernet” means forwarding is configured, not a WAN reachability test.

## Ownership and teardown

P4 owns USB host and transfers Ethernet frames to the C5 router over SPI.
The driver, service and LVGL screen are separate modules. A synchronized snapshot
is the only presentation interface. The screen registers a close hook and an
idempotent delete fallback. All UI objects and the refresh timer precede start.
Content uses chrome bounds, flexible wrapped labels and the selected theme for
both 240x320 and 320x240; overflow remains scrollable. Physical layout validation
on both orientations is still required.

The worker holds a no-sleep lease, claims the PHY, waits for external VBUS removal,
sets HUSB320 source role and installs USB host before switching the mux.
VBUS is enabled only after a consistent attached-sink sample. BQ25896 verifies
battery voltage >=3500 mV, no external VBUS and no boost fault; output is 4998 mV
with a 1400 mA startup boost limit. Battery/fault telemetry is checked every second.
USB-C attachment is sampled every 50 ms, independently of packet rate. The HUSB320
driver retries one timed-out read with an entirely fresh sample; forwarding does
not continue during that retry. A second timeout still stops sharing, and a valid
detach disables VBUS. See [Type-C handling](../tusb_desc/README.md#type-c-role-and-power).
A periodic power-check failure retains its specific reason, battery voltage and
latched/live fault bytes in the screen snapshot until the next session. The log
also records ADC validity, boost, thermal and OTG flags before teardown. An
unavailable snapshot is distinguished from low voltage or a hardware fault;
post-teardown telemetry cannot establish which guard originally fired. Invalid ADC
samples fail closed as well. This diagnostic does not establish a hardware fix.
The board source ceiling is 1500 mA (owner-specified); BQ25896's highest selectable
step below it is 1400 mA, not 1650 mA. This is independent of charging/input limits.
Firmware-only device profiles live in
[`ethernet_power_profiles.h`](../../firmware_p4/components/Service/ethernet_share/include/ethernet_power_profiles.h).
AX88179B `0b95:1790` retains 1400 mA after enumeration; other CDC-NCM devices use
500 mA. Change the table to add a device or its current budget. Voltage remains
4998 mV. Requests outside 500..1500 mA fail; intermediate values round down.
All adapters initially share `ETHERNET_POWER_STARTUP_MA`: their VID/PID cannot be
read while unpowered. Profiles apply after CDC-NCM setup and before forwarding,
including reconnects. The higher startup budget is therefore not restricted by
VID/PID. Type-C still advertises default USB current, not a guaranteed 1.5 A.
The adapter started and forwarded traffic with this setting in the test below;
electrical verification of the actual output/current limit remains pending.
Generic OTG commands remain guarded. USB input-budget polling is suspended during
source ownership, and charging cannot be enabled while host/source is active.

The service requires the peer's whole-packet capability before acquiring USB.
It exchanges a complete packet in each direction per SPI operation. C5 RX
congestion drops the outgoing packet but still delivers a queued incoming one.
Confirmed control-command BUSY responses are bounded; START and packet exchanges
are never resubmitted as new commands after ambiguous results. An isolated
exchange timeout abandons both packets and checks router status before continuing.
A successful exchange clears the streak; three consecutive timeouts stop sharing.
Local bridge errors, invalid replies and failed status checks remain fatal.

The worker yields one scheduler tick (1 ms in this build) after each exchange,
including under continuous traffic, for board leases and Companion. An empty
exchange adds a 2 ms idle wait, replacing the former 20 ms wait. Within the
original absolute deadline, the transport can resend a byte-identical request
up to three times; C5's cache prevents redispatch. Peer restart rejects the old
session. See [SPI transport](../spi_bridge/README.md#ethernet-frame-transport).
A link loss during USB transmit returns to normal session teardown. The UI
identifies packet exchange as Packet forwarding and USB sends separately.

`test_forwarding.py` covers control retry/stop and timeout bounds.
`test_packet_exchange.py` exercises the production P4 exchange and C5 queue
handler with host mocks, including duplex congestion, stale tokens, malformed
replies, timeouts and lock release. Both live in `tools/usb_ethernet/` and run
with address/undefined-behavior sanitizers. These do not establish throughput
or electrical reliability on hardware.

Teardown requests C5 stop, disables VBUS, removes USB tasks, restores the UART
mux and sink role, then frees buffers and releases the no-sleep/PHY leases.
Failed cleanup retries while retaining ownership. C5 independently expires the
session if its five-second master heartbeat disappears. Unplug/link loss stops
routing and a fresh session is created on reconnection.

Native USB roles can be reused without reboot. After leaving Storage, HID or audio,
the host claim serializes with device I/O, disconnects TinyUSB, drains callbacks,
uninstalls its task/stack/PHY and releases the CDC transport. Returning from host
removes host resources and restores the UART mux/sink role; opening a USB device
function installs TinyUSB and its CDC callbacks again. Active HID/MSC/audio sessions
must finish normally first; a role change never cuts an in-flight SD write.
A teardown timeout reports busy and permits retry. A partial SDK uninstall failure
is isolated rather than pretending the PHY is free; it remains an error requiring
recovery, unlike the normal device/host transition.
An external powered hub is not assumed safe to backfeed the Highboy USB input.

## Deployment and validation

Update **both P4 and C5 applications** from the same build. No new SD asset is
required. Existing flash/deployment tools and partition layouts are unchanged.
The implementation adds neither a public console command for unrestricted VBUS
nor automatic host startup at boot. `ethernet` reports a local read-only snapshot,
including the retained failure detail, without accessing SPI or I2C.

### Reported AX88179B test, 2026-11-27

After installing the P4 transport recovery/admission changes, the owner connected
the adapter and a phone and reported working Internet access, then intentionally
stopped sharing. Bluetooth console readback confirmed idle state with `ESP_OK`,
Ethernet IP `192.168.2.161`, RX 2,685,586 bytes and TX 1,714,336 bytes. Retained SPI
failure counts were zero for board, Ethernet and other commands after clearing
before the test. Those counters count failures, not successful internal retries.
The Companion remained connected; background log relay was disabled during this
first run. A subsequent run with log relay enabled transferred RX 9,417,565 / TX
2,395,300 bytes, then failed: retained diagnostics showed Companion HOST_TX
response timeout and Ethernet/board lock timeouts. Therefore the first successful
run did not establish sustained reliability with logs. The follow-up extends
same-transaction transport recovery to HOST_TX/HOST_STATUS and handles the
adapter's repeated speed notification without log flooding; see
[the CDC compatibility source](../usb_ethernet/README.md#scoped-cdc-ncm-compatibility-source).
The follow-up P4 was built, flashed to the inactive OTA slot, verified and rebooted.
With Companion log relay enabled, the next run reached DHCP and forwarded
RX 3,848,779 / TX 1,095,094 bytes before a separate USB-C attachment timeout at
222,577 ms uptime. No repeated `0x2A` warnings appeared. A single stream-poll
timeout recovered without ending sharing; retained board/Ethernet/command failure
counts remained zero. Internal heap during forwarding was 25,163 bytes free,
18,628 minimum since boot. This run does not establish sustained reliability.
It exposed the 15 ms attachment read ending the session on the first timeout;
the bounded fresh-read retry and packet-rate-independent attachment sampling
passed host fault-injection tests and a P4 build. The application was flashed to
the inactive slot, verified and rebooted with Companion log relay enabled;
the repeat session remained active for six minutes before intentional Back/reentry.
A snapshot after 312 seconds of that session reported `ESP_OK`, RX 4,872,719 and
TX 1,540,315 bytes. The owner confirmed phone browsing. Display dim/sleep/wake
occurred without ending forwarding. Reentry without reboot recognized the adapter
and restarted DHCP/sharing on the first attempt; after 98 seconds its snapshot
reported `ESP_OK`, RX 3,437,397 and TX 1,029,064 bytes. Board, Ethernet and other
command failure counters stayed zero through reentry (not cleared between runs).
Adapter-ready internal free heap was 25,731 bytes initially and 25,727 on reentry;
a live sample was 25,575 free, with 18,628 minimum since boot.

No sharing-stage failure or repeated speed-notification warning appeared in
these two sessions. Two separate SYSTEM_STREAM polls timed out at 104 ms and
recovered without ending the Companion connection or sharing; these stream
warnings are not counted by the retained command-failure groups. This is a bounded
hardware test, not a long-duration reliability or full lifecycle qualification.

### Whole-packet / 10 MHz validation, 2026-11-27

Both applications were built, flashed to inactive OTA slots and verified.
The normal SPI clock read back as 10,000,000 Hz. After the stream-poller fairness
change, 300 read-only PING/version/status requests passed with no failures
(2,020 ms total; 9,111 μs worst request). This is a control-link check, not a
network throughput benchmark. The first whole-packet phone browsing session
reported RX 15,065,331 / TX 3,470,654 bytes on intentional stop, with zero retained
board/Ethernet/other command failures. Companion command delivery stalled during
that run; the poller now waits on the mutex instead of skipping an in-flight
Ethernet request. The follow-up session transferred RX 21,095,868 / TX 4,400,524
bytes and stopped intentionally with `ESP_OK`; the owner reported faster browsing.
Companion commands still stopped responding while sharing and recovered after
sharing stopped, without a reboot. Retained diagnostics recorded one Ethernet
exchange lock timeout and one power-state response timeout. Therefore mutex
admission alone did not resolve Companion coexistence. The owner declined a
4 MHz comparison and requested keeping 10 MHz; no measured Mbps comparison or
sustained Bluetooth/forwarding reliability claim is established by these runs.

`python3 tools/usb_ethernet/test_power_guard.py` checks the voltage boundary,
invalid snapshots/ADC, boost and thermal faults, and retained diagnostic text.
It does not simulate electrical behavior.

Host tests cover the NCM codec, production Type-C role sequencing, mux ownership,
charger source guards and input-policy isolation. Firmware builds are compile
validation, not hardware certification. Before claiming support on the actual
adapter, verify enumeration, DHCP, a client obtaining hotspot DHCP/DNS, Internet
traffic, link unplug/replug, Back during every wait state, C5 reset and repeated
entry/exit. Measure runtime heap and stack minima under sustained traffic.
The default SPI clock is now 10 MHz (previously 4 MHz). This raises the raw
wire bit rate 2.5×; application throughput also includes request/response framing,
polling, scheduling, Wi-Fi and TCP/IP. No 2.5× Internet throughput claim follows
from the clock alone. Whole-packet forwarding replaces up to seven RX and seven
TX fragment commands with one exchange. Both applications must be rebuilt and
flashed; partition tables and SD assets do not change. Throughput and stability
at this clock still require hardware validation; USB gigabit branding is not a
throughput promise for this device.

See [USB driver](../usb_ethernet/README.md) and [C5 router](../ethernet_router/README.md).
