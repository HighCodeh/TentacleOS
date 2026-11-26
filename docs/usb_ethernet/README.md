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

# USB Ethernet driver

P4 `Drivers/usb_ethernet` implements CDC-NCM NTB16 using the Component Manager's
`espressif/usb_host_cdc_acm` transport. It does not contain an ASIX vendor driver.
The owner's AX88179B (`0b95:1790`) exposes CDC-NCM on the Mac; its descriptors
were inspected there. External USB hub support is enabled for adapters behind hubs.
ASIX documents the class modes on its [AX88179B product page](https://asix.com.tw/en/product/USBEthernet/Super-Speed_USB_Ethernet/AX88179B).

The enumeration filter selects configuration 2 for multi-configuration
`0b95:1790` adapters, where AX88179B exposes CDC-NCM. Configuration 1 uses
ASIX vendor framing and is not supported by this driver. Single-configuration
devices and other VID/PIDs retain the SDK default configuration, including hubs.
The configuration-2 NCM mapping is also recorded in this
[Linux networking report](https://lists.openwall.net/netdev/2025/10/11/8).
The detected configuration is logged before class-driver setup; bounded hardware
DHCP/forwarding observations are recorded in the
[sharing validation](../ethernet_share/README.md#reported-ax88179b-test-2026-11-27).

## Contract and ownership

Only the Ethernet-sharing service worker calls start/poll/receive/transmit/stop.
USB callbacks copy frames into an eight-entry queue; they never perform SPI I/O.
Each frame is at most 1514 bytes. NTBs are limited to 2048 bytes, use no CRC,
and are validated completely before any frame is delivered. Cycles, overlapping
regions, invalid lengths and truncated tables are rejected. TX padding forces a
short USB packet without relying on an unavailable CDC transport ZLP option.
The negotiated alignment must fit a full frame in the bounded NTB.
After negotiating NTBs the driver explicitly sends `SET_INTERFACE` for the
adjacent CDC data interface's two-endpoint alternate setting and verifies it
with `GET_INTERFACE`, then installs the packet filter. Reserving endpoints in
ESP-IDF's host library does not send this request to the adapter. The matching
CDC transport supports adjacent control/data interfaces; other layouts are
rejected. Link notifications are logged on transitions.
See the [Linux NCM initialization](https://github.com/torvalds/linux/blob/master/drivers/net/usb/cdc_ncm.c)
for the distinction between claiming an interface and activating its setting.

Queue storage and TX staging live in explicitly allocated PSRAM. USB-DWC transfer buffers and transfer descriptor lists use the ESP32-P4 SDK's
`CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM` path, including SDK cache maintenance.
The USB event task has a checked 6144-byte PSRAM stack and uses `vTaskDeleteWithCaps`;
it performs no flash/filesystem writes. The class-driver task retains its internal
6144-byte stack. Task controls, synchronization objects, ISR data and the DWC frame
list remain internal as required by the SDK.
Host startup and adapter-open logs report internal free heap and largest block
for comparison with and without the Companion connected.
Stop closes the device, uninstalls the class driver, drains host devices, stops
the host daemon, uninstalls the library and frees packet storage, in that order.
A failed stop retains ownership for retry rather than freeing active buffers.

Supported: CDC-NCM NTB16 with Ethernet MAC string and link notification, IPv4
Ethernet frames. Not implemented: vendor ASIX framing, ECM, RNDIS, VLAN jumbo
frames, NTB32 or simultaneous USB device and host roles.
`usb_ethernet_identity()` exposes the configured adapter VID/PID to the service
worker for firmware power-profile selection; it returns false before setup or
after disconnect. See [sharing lifecycle and power](../ethernet_share/README.md).

## Scoped CDC-NCM compatibility source

The Component Manager dependency remains `espressif/usb_host_cdc_acm ==2.4.0`
(upstream esp-usb revision `d2674397bc5ce0b5d9262fb179fdb06c1090801c`, Apache-2.0).
That revision reports the standard CDC `CONNECTION_SPEED_CHANGE` notification
(`0x2A`) as unsupported and logs a hex dump. The AX88179B was observed sending
it every 64 ms, flooding Companion's shared SPI/BLE log path.

`Drivers/usb_ethernet/cdc_compat.cmake` runs
`tools/usb_ethernet/prepare_cdc.py` after dependency resolution. The generator
checks the original `cdc_acm_host.c` SHA-256
`88bd7d7ffa42f6f5c76f7ec9880f7cbc63a4aabb6e480dc6af0a72a7277b6617`
and writes the adapted translation unit under
`build/components/Drivers/usb_cdc_ncm/`. Only that unit replaces the original in
the managed component's target; includes, other sources and dependency resolution
remain upstream. Managed files and their Apache copyright/license are preserved.

The patch validates the notification header before reading it and accepts a
speed notification with its eight-byte rate payload without logging it. Link
state still comes only from NETWORK_CONNECTION; speed information neither changes
link state nor confirms Internet access. Malformed and unknown notifications keep
warnings. No global log level is lowered. On dependency upgrade, review upstream
handling, remove this override if fixed, otherwise requalify the source hash and
regression tests. Never edit the generated translation unit.

`python3 tools/usb_ethernet/test_cdc_notifications.py` executes the generated
notification handler with 10,000 valid speed notifications, truncated headers,
malformed speed payloads, unknown notifications and link callbacks; a modified
upstream source is rejected. This is host validation, not a USB bus stress test.

## Validation

Host test (no device required):

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Ifirmware_p4/components/Drivers/usb_ethernet/include \
  tools/usb_ethernet/test_ntb.c \
  firmware_p4/components/Drivers/usb_ethernet/usb_ethernet_ntb.c \
  -o /tmp/highboy-test-ntb
/tmp/highboy-test-ntb
```

`python3 tools/usb_ethernet/test_setup.py` checks data-interface selection,
activation request ordering, readback validation and failure propagation using
host mocks of the USB control transport.

The NTB test checks all Ethernet lengths and four alignment remainders, truncation,
malformed chains, overlapping tables and a deterministic mutation corpus.
It does not prove enumeration, PHY power, DHCP or forwarding on Highboy hardware.
