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

# Port scanner

Wi-Fi → Port Scan opens a configuration screen; entry never starts network traffic.
An established C5 station connection with a valid IPv4 address is required.

## Targets and ports

| Target | Address field |
| --- | --- |
| Gateway | Current station gateway, resolved when Start is pressed |
| Single IP | One dotted-decimal IPv4 address |
| Local network | Current station address and subnet mask |
| IP range | Inclusive `192.168.1.10-192.168.1.40` |
| CIDR | `192.168.0.0/16`, `192.168.1.0/24`, or another prefix from 0 to 32 |

Local-network and CIDR scans omit network/broadcast addresses for prefixes below
/31; /31 keeps both addresses and /32 scans one host. Explicit ranges are inclusive.
CIDR normalizes host bits; a /16 covers 65,534 hosts. Broad ranges can take a long
time: progress counts completed hosts and ports on the current host, without a
20-second deadline on the entire scan.

Ports can be Common (the existing SD/assets list with built-in fallback), Custom,
or All 1–65535. Custom accepts comma-separated ports and inclusive ranges, for
example `22,80,443,8000-8100`. Ports are validated, deduplicated and sorted.
The existing C5 engine performs **TCP connect scans only**. An open result means a
connection succeeded; an optional banner contains bytes received from that service.
This does not perform UDP scanning or active service/version identification.
The bundled Common list and built-in fallback both include SSH port 22; a
microSD override replaces that list, so its contents determine which ports are
selected. Common and Custom use the same TCP engine.

**Ping first** is off by default. When enabled, up to eight consecutive IPv4
addresses are probed together. Each address retains two attempts with its own
400 ms reply window; only unanswered addresses are retried. Discovery completes
the entire selected interval before any TCP scan starts. P4 retains responding
addresses in a temporary PSRAM bitmap, then scans those hosts in ascending IP
order, completing all selected ports on one host before moving to the next.
No reply increments Skipped. When disabled, every selected address receives the TCP scan
directly, without a discovery phase or host bitmap.
A host blocking ICMP can still be online, so enabling the filter trades coverage
for speed. Socket/transport failures stop with an error instead of classifying a
host as absent.

For a silent /24, 254 addresses require 32 batches: nominal reply-window time
falls from 203.2 seconds to 25.6 seconds. This is a calculation, not a hardware
benchmark; SPI polling, scheduling, ARP and TCP scanning add time. Responsive
batches finish without waiting for the remaining timeout once all hosts reply.

## UI and results

UP/DOWN selects a configuration row; LEFT/RIGHT or OK changes a mode. OK on
Address or List / range opens the contextual keyboard. **Scan** opens a dedicated
execution view before starting the worker. It shows Ping/Scanning/Stopping or the
final state, the current IP, completed hosts/ports, open/saved/skipped counts and
a progress bar. Open ports appear as a flat list with IP:port and a second-line
banner. UP/DOWN browses the recent results; OK opens details. Following the latest
entry resumes when navigating to the end. There are no status rows in the setup menu.
With Ping first enabled, **1/2 DISCOVERY** shows probed/total IPs and responding
hosts. `PING x8` (or the smaller final batch size) identifies the number of
addresses currently being probed; the displayed IP is the first address in that
batch. **2/2 TCP SCAN** then shows completed/responding hosts and ports on the
current host. The progress bar measures the current phase, restarting for TCP;
an entirely silent interval finishes with "No hosts replied to ping".

The first BACK while running requests cancellation and keeps the execution view
and its results visible. A second BACK returns to setup, even while cleanup is
pending; a new scan is blocked until the worker exits. After normal completion,
BACK also returns to setup. Programmatic navigation and deletion cancel owned
work. Cancelling or failing during discovery starts no TCP scans. Cancellation
during TCP retains open-port results collected up to that point. No UI timer,
keyboard or LVGL object is retained by a worker. The live view
uses theme colors and chrome bounds, rebuilding on theme/size changes; it shows
four result rows at 240×320 and two at 320×240. Required widgets exist before the
scan starts; failed initialization cancels work and keeps BACK usable.

Each scan uses a CSV under `/sdcard/wifi/port_scans/`, named
`scan_<first IPv4 in hex>_<last IPv4 in hex>.csv`. The key is the resolved inclusive
address interval, independent of port selection, ping mode and SSID. Equivalent
local/CIDR/range selections replace the same archive; scanning a single IP does
not replace the whole subnet. A new scan replaces the previous archive after
target/port validation and checked header creation through `storage_write_atomic`.
Cancelling retains the new partial results, including an empty result set if no
open ports were found; it does not restore the previous scan. Header creation
failure preserves the prior file until the storage helper's rename stage.
Every
validated open TCP result is appended in bounded batches; the saved count advances
only after checked writes, flush and close. The most recent **128 open ports** also remain
in PSRAM as a live preview. That preview limit does not limit the archive.

After completion or cancellation, Results opens the current archive's IP list.
Saved scans opens **networks/intervals → IPs with open ports → TCP ports → banner
details**, available after reboot without a Wi-Fi connection. CIDR-sized intervals
show network/prefix labels; other intervals show their bounds, and single-host
scans show the IP. UP/DOWN selects an entry, OK opens it, LEFT/RIGHT changes pages
directly and BACK returns one level. There are no Previous/Next menu items. Each
page contains up to eight entries. The IP list shows each address once with its
open-port count, including ports beyond the 128-entry preview. Hosts with no open
ports are not listed; the scan itself can still be opened and deleted when empty.
Opening a saved file never starts a scan. Navigation cancels outstanding reads.
Delete scan in the IP list opens a confirmation (Cancel initially selected), then
removes the entire archive on the service worker. Success returns to the networks
list; failures stay visible and allow retry. Deletion is rejected while scanning
or another archive operation owns the worker. Deleting the current archive also
clears its retained preview/path so Results cannot resurrect it.
Old timestamp-named archives remain accessible under Legacy labels with the same
IP/port hierarchy and deletion. They do not contain their requested interval, so
they are not automatically replaced by a new scan.
Archive browsing waits until the scan stops; ongoing scans use the RAM preview.
Draft scan settings survive screen re-entry during the current boot.

If no archive can be created (missing/unavailable/full SD), scanning continues
with an explicit warning and the 128-entry preview limit. A write/flush/close
failure after creation stops further scanning and appears on the screen; earlier
saved records remain available. Cancellation preserves collected results. An
archive records observations, not proof that the entire requested scan completed.

CSV columns are `ip,port,protocol,status,banner_hex`. Fixed-width 160-byte records
allow direct seeks to a page without an index or loading the whole file. The
banner's 64 bytes are hex encoded so commas, quotes, control bytes and newlines
cannot corrupt CSV rows; the viewer decodes the banner. The reader validates the
header, IP, port, protocol/status and hex encoding. A truncated final record is
ignored and the page is marked partial; malformed complete records report an
error. Files are bounded by available card space and a 2 GiB seek limit (over
13 million records), with a reported error on reaching the latter. Do not change
CSV padding/line endings or host ordering if the file must be reopened on the device.
Host groups are contiguous and sorted by numeric IPv4 in files produced by the
scanner. The reader locates group boundaries by binary search over fixed records;
LEFT finds the preceding eight groups and RIGHT resumes at the next record cursor.
This skips a host with 65,535 ports without loading its rows or building a growing
RAM index. Port pages seek within the selected host's checked record range and
validate its IP. A malformed record encountered during lookup/read reports an error.

## Service and transport

`Service/port_scanner` owns a pinned core-0 worker, Wi-Fi resource lease and no-sleep
lease. The job, port bitmap and bounded retained results use checked PSRAM
allocations. Ping discovery adds one bit per selected address: 32 bytes for /24,
8,192 bytes for /16. This bitmap is allocated before replacing the SD archive or
sending probes and is freed on completion, error or cancellation. Insufficient
PSRAM fails with `ESP_ERR_NO_MEM`, without internal-RAM fallback or a truncated
interval. Very broad intervals may exceed available PSRAM. The bitmap exists only
for the current run; the persistent archive contains open TCP results, not a
resumable list of ping responders. The worker's 6 KiB stack uses checked internal RAM: the common-port
list can fall back to LittleFS assets, whose flash reads disable caches and cannot
run on a PSRAM stack. Task creation failure releases the job and reports failure
before acquiring radio or power leases. The stack is released when the worker
exits; synchronization/status objects remain internal. Applications/UI only presents
snapshots and submits configuration; it does not call SPI or load port files.
Archive pages and deletion run on a separate, short-lived core-0 service worker with a
checked 6 KiB internal stack and no-sleep lease. Scans and archive readers are
mutually exclusive. Page jobs and one retained page snapshot use bounded PSRAM;
the UI uses a PSRAM page buffer. Neither worker retains UI pointers. Closing a
screen cancels directory iteration/page reading and releases the lease on exit.
No file I/O runs in LVGL callbacks, including deletion confirmations. Files are closed between batches/pages; the
existing FatFS/USB Storage ownership checks govern access to the card.

The worker sends the existing target-list SPI operation in batches of at most
32 ports for one host. This matches the C5 result capacity and avoids truncating a
scan when all ports in a batch are open. Iterating hosts on P4 supports /16 without
expanding C5 tables or relying on its legacy 256-address network operation.
Each batch has a 45-second watchdog, with bounded RPCs and cooperative cancellation.
Inside each 32-port request, C5 connects to up to eight ports concurrently. A
`select()` event resolves only the sockets reported ready, checking `SO_ERROR`;
pending connections remain watched until completion or the existing one-second
connect deadline. A fast RST on another port cannot discard a later SSH response.
The deadline is absolute, so early readiness and interrupted waits cannot restart
the timeout. Waits use at most 50 ms slices to observe STOP. All sockets close on
completion, timeout, cancellation and wait errors; already confirmed open ports
remain reportable on cancellation without waiting for banners. Banner reads and
the existing HTTP fallback use checked, bounded send/receive timeouts; no banner
does not change an established connection's OPEN result.
A transport failure initiates STOP and retains ownership until the C5 confirms idle;
results already collected remain readable. The UI may leave while this cleanup
continues. With ping enabled, P4 starts `SPI_ID_WIFI_HOST_PROBE_BATCH_START`
(0x015E, 8-byte packed first IPv4/count/reserved request), waits for the shared
async runner, and reads `SPI_ID_WIFI_HOST_PROBE_BATCH_STATUS` (0x015F, 12-byte packed
error/first IPv4/reachable mask/count/running snapshot). The IPv4 is network byte
order; bit i represents first IPv4 + i. P4 validates the echoed range and mask.
Every ping and TCP batch waits for confirmed C5 runner completion. All ping
batches finish before the first TCP batch; the two phases never overlap.
STOP (0x014D) cancels both ICMP and TCP work. These are additive operations in both
paired-v2 headers; the original single-address 0x015C/0x015D layouts remain available.
Update both apps for batch discovery; an older C5 reports an unsupported operation.
The two-phase coordinator and its progress UI require only a P4 update if C5
already supports those batch operations.
The TCP readiness correction runs on C5 and requires updating C5; a P4-only flash
does not install it. It does not change the SPI protocol or port selection lists.

`Service/wifi/wifi_host_probe` runs on the existing C5 radio scan runner, using
one short-lived raw ICMP socket per batch and bounded scratch on the existing
internal worker stack. Eight hosts fit below the SDK's ten-entry ARP table;
immediate replies are drained between sends to reduce raw mailbox pressure.
It validates source, IPv4/ICMP lengths, checksum, identifier, sequence and nonce.
Out-of-order replies and late replies to either sent attempt in the current batch
are accepted; duplicates do not count twice. Receive waits are 50 ms slices so
cancellation is cooperative; the runner prevents idle sleep. The socket closes
on success, cancellation and failure. Cancellation retains the partial reply mask
and earlier open-port results without starting further TCP scans.
No task is created per address. Reset is called only before exclusive runner start;
concurrent probes are rejected before changing the request or cancellation state.

C5 rejects overlapping scan requests before modifying its shared request storage;
its abort flag is atomic across the command and scan tasks. Flash both updated apps
for these lifecycle guarantees. Build alone does not validate network timing on hardware.

`wifi portscan --ip ...` retains its existing common/min/max arguments and blocking
console behavior, but uses this same coordinator and result set. It no longer links
a second scan/fetch loop or silently stops at 20 seconds. The legacy P4 helper API
remains available to external callers, with its existing limits; UI and console do
not use that path. The coordinator uses size optimization (`-Oz`), alongside the existing compact
UI policy. P4 now needs the expanded slots and compressed assets described in
[the storage migration contract](../highboy/README.md#flash-and-storage-boundary).
C5's partition map is unchanged; both apps need updating for the ping operations. Per-batch C5 progress
is DEBUG-level to avoid serial-log flooding;
scan lifecycle and results remain available through service/UI feedback.

## Validation

- `python3 tools/highboy/tests/test_tcp_port_scan.py`: production C5 TCP engine
  and P4 Common loader with timed fake sockets under ASan/UBSan. A fast refusal
  alongside delayed SSH reproduces the former missing-port bug. Both the bundled
  Common file and built-in list now retain SSH/AirTunes results. Cases cover
  Custom selection, staggered readiness, near-deadline replies, silent ports,
  interrupted/failed waits, immediate connects/refusals, socket/setup failures,
  cancellation, result capacity and banners. Every allocated socket must close.
  The aggregate host runner includes this regression; no network access occurs.
- `python3 tools/highboy/tests/test_port_scanner.py`: production service with mocked
  SPI, allocation/task failures, invalid addresses/ports, /16, /31, /32, actual
  gateway/subnet, complete 1–65535 coverage, deduplication, result saturation,
  cancellation, malformed responses, slow batches and lost responses; ping on/off,
  skipped hosts, batched discovery (8/8/3 and 254 hosts in 32 batches), malformed
  range/mask replies, probe errors/cancellation and rolling preview ordering.
  Assertions enforce full-interval discovery before the first TCP start and idle
  acknowledgement before every following batch. Tests cover two responding hosts
  with all 65,535 ports, a full /16 discovery, bitmap allocation failure without
  replacing an archive, and cancellation in either phase. The common
  port-loader mock enforces the flash path's internal-stack requirement, including
  a complete local /24 scan; bulk allocations remain checked PSRAM allocations.
  The production archive module runs against temporary host files: persistence,
  paging beyond 128 and 65,535 entries, IP group boundaries in both directions,
  65,535-record hosts, target labels, deterministic replacement and partial rescan
  retention, checked/cancelled deletion, cancellation/re-entry, truncated/corrupt
  files, path validation and injected write/flush/close failures are covered.
- `python3 tools/highboy/tests/test_port_scanner_ui.py <native-lvgl-build-directory>`:
  production screen/menu with real LVGL, both orientations and two themes;
  configuration, keyboard, live/archive paging, reader errors, pending-read
  cancellation, setup/execution transitions, first-BACK stop/second-BACK setup,
  LEFT/RIGHT without virtual navigation rows, network/IP/port hierarchy, deletion
  confirmation/failure/success and last-page removal, live theme changes,
  phase-specific counters/progress, cancelled discovery, no responding hosts,
  bounds/rendering and repeated entry/exit. Uses the same
  prepared LVGL directory as `test_connection_settings_ui.py`.

- `python3 tools/highboy/tests/test_wifi_host_probe.py`: production ICMP probe
  with fake sockets and clock: eight silent hosts complete their windows in 800 ms
  on one socket, selective retries, per-host deadlines, late/out-of-order/duplicate
  replies, wrong source/nonce/checksum, malformed lengths, partial cancellation
  and socket failures under ASan/UBSan. Extracted production dispatcher cases also
  check range/reserved validation, overlap rejection, status publication and STOP.
  No network traffic; mock timing does not measure device performance.

The initial P4 image exceeded the previous 0x300000-byte slot by 3,072 bytes.
The current 0x310000-byte slots require the partition/assets migration above;
the guarded app-only updater rejects the earlier layout. Host validation passes,
including real LVGL at both orientations; both ESP-IDF v6.1 targets build. The
P4 size check passes with the expanded slots. A hardware local-network scan exposed
a cache-disabled assertion with the former PSRAM worker stack; validation of the
internal-stack correction, ICMP filtering and SD archive I/O on hardware remain pending.

These are host tests without serial/network access. Hardware acceptance should
compare a small known host/port range against expected listening services, repeat
with the local subnet and an explicitly chosen CIDR, then cancel and reopen.
