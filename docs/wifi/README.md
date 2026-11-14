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

# Wi-Fi service

## Progressive AP scanning on Highboy

AP discovery screens use `wifi_live_scan` and the shared `wifi_live_scan_ui`:
connection settings, Survey, ESP Scan, channel analysis and AP selection for
signal locator, target clients, attack selection and Evil Twin. Discovery runs
continuously by default. APs appear after each channel completes, with a 200 ms
UI refresh; this is channel-by-channel publication, not a callback per beacon.
UP/DOWN browses the growing list. BACK stops scanning and retains the discovered
APs in the original results/selection screen. BACK there follows that screen's
normal navigation. LEFT/RIGHT pages through AP selection lists. Survey retains
its explicitly selected single-sweep and timed modes.

The radio worker runs on core 0; all LVGL updates run on the UI thread. Results
are deduplicated by BSSID, with up to 128 records and RSSI observations in PSRAM.
Insertion order stays stable during discovery, and hidden beacons cannot erase
a previously discovered SSID. At capacity, existing records still update. A
stopped snapshot is saved through the existing SD-only AP JSON persistence path.

C5 scans one channel per asynchronous request with 120 ms dwell: active on 2.4 GHz,
passive on 5 GHz, subject to the driver's country/channel restrictions. The paired
v2 SPI headers add Wi-Fi operations `0x56` (start slot), `0x57` (busy/error/count)
and `0x58` (indexed, 42-byte `spi_wifi_scan_record_t`). C5 keeps a dedicated
compact completed-channel snapshot containing SSID, BSSID, RSSI, channel and
authentication mode, avoiding a second SDK-native AP array in internal RAM;
P4 validates response sizes and merges it before advancing. Cancellation drains
the in-flight channel, then stops; failures preserve already collected records.
A new session is rejected until the old worker has finished. Legacy console
one-shot scans and monitor-mode capture remain separate.

**Both P4 and C5 applications must be updated for this interface.** P4 alone
cannot enable progressive discovery on an older C5. No flash is performed by
building either target.

Validation: `python3 tools/highboy/tests/test_wifi_live_scan.py` runs the production
P4 worker and C5 channel function with host mocks and sanitizers, including stop,
reentry, malformed replies, capacity, deduplication, statistics and band restore.
On hardware, check each screen in both orientations: APs arrive while scanning,
BACK retains selectable results, another BACK exits, and repeated entry/exit
leaves no scanner running. Verify both bands and connection after stopping.


The [ESP SCAN screen](quick_scan.md) uses progressive AP discovery and MAC
vendor table, ranking BSSID/OUI/RSSI snapshots locally on P4. It does not invoke
the separate headless monitor-mode capture backend.

Split across **both firmwares**. The **C5 owns the real radio** (the full esp-idf
Wi-Fi stack, AP/STA, scanning, promiscuous capture, config persistence). The **P4
is a pure proxy**: every public call forwards to the C5 over the SPI bridge. Keep
this split in mind - the two `wifi_service` APIs look almost identical, but the
P4 one has no local Wi-Fi stack behind it.

Both headers share the same file-path defines:

```c
#define WIFI_AP_CONFIG_FILE      "config/wifi/wifi_ap.conf"
#define WIFI_KNOWN_NETWORKS_FILE "storage/wifi/know_networks.json"
```

Both are relative paths handed to the `storage_assets` API; the actual files
live on the C5 (it owns storage for Wi-Fi state).

---

# P4

`firmware_p4/components/Service/wifi/` - a **thin SPI-bridge proxy** to the C5.
There is **no local esp-idf Wi-Fi stack on the P4**: no APSTA mode, no NVS init,
no default event loop, no netif / static IP / DHCP server, no `cJSON`, no local
channel-hop task, and no LED signalling in this module. Every operation is a
`spi_bridge_send_command` (or `spi_bridge_run_scan`) to the C5.

## How state is read without blocking the UI

The header getters (`wifi_service_is_active` / `wifi_service_is_connected`) are
polled from an `lv_timer` on the LVGL thread. Doing a blocking SPI RPC there
froze the renderer whenever the bridge was busy (e.g. during a scan, which holds
the bridge mutex for seconds). So a background `status_poll_task` (pinned to
`SYS_CORE_RADIO`, priority `SYS_PRIO_BACKGROUND`) polls `SPI_ID_SYSTEM_STATUS`
once per second and caches `wifi_active` / `wifi_connected` into volatile flags;
the getters just read those flags. The same poll also feeds
`bluetooth_service_set_running_cached`.

## API Functions

### Initialization & Lifecycle

#### `wifi_service_init`
```c
void wifi_service_init(void);
```
Spawns the background `status_poll_task` (once). It does **not** bring up any
radio - the C5 owns that.

#### `wifi_service_deinit`
```c
void wifi_service_deinit(void);
```
Sends `SPI_ID_WIFI_STOP` to the C5 (functionally identical to
`wifi_service_stop`; there is no local stack to tear down).

#### `wifi_service_start` / `wifi_service_stop`
```c
void wifi_service_start(void);
void wifi_service_stop(void);
```
Forward `SPI_ID_WIFI_START` / `SPI_ID_WIFI_STOP` to the C5.

### Scanning

#### `wifi_service_scan`
```c
esp_err_t wifi_service_scan(void);
```
**Async, proxied.** The C5 runs the scan. This calls
`spi_bridge_run_scan(SPI_ID_WIFI_SCAN, SPI_ID_WIFI_SCAN_STATUS, NULL, 0)`, which
kicks the scan and polls the scan-status id **without holding the bridge mutex**
(so the UI and other peripherals stay free), returning once the C5 reports
completion. Returns `esp_err_t`.

#### `wifi_service_get_ap_count`
```c
uint16_t wifi_service_get_ap_count(void);
```
Fetches the last-scan count from the C5 via `SPI_ID_SYSTEM_DATA` (with the
`SPI_DATA_INDEX_COUNT` magic index). Returns 0 on bridge failure.

#### `wifi_service_get_ap_record`
```c
wifi_ap_record_t *wifi_service_get_ap_record(uint16_t index);
```
Fetches one scan record from the C5 (`SPI_ID_SYSTEM_DATA`, indexed) into a static
cache and returns a pointer to it, or `NULL` on failure. Sanitizes the SSID at
this single point for every consumer (console + UI): non-printable / non-ASCII
bytes become `?` (they hang LVGL's text renderer), and an empty SSID (hidden
network) is replaced with the placeholder `[rede oculta]`.

### Connection & Management

#### `wifi_service_connect_to_ap`
```c
esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password);
```
Packs the SSID/password into `spi_wifi_connect_t` and forwards
`SPI_ID_WIFI_CONNECT` to the C5. The C5 performs the actual join and known-network
persistence.

#### `wifi_service_is_connected`
```c
bool wifi_service_is_connected(void);
```
Returns the cached `wifi_connected` flag (refreshed by `status_poll_task`). Does
**not** hit the bridge.

#### `wifi_service_is_active`
```c
bool wifi_service_is_active(void);
```
Returns the cached `wifi_active` flag (refreshed by `status_poll_task`). Does
**not** hit the bridge.

#### `wifi_service_get_connected_ssid`
```c
const char *wifi_service_get_connected_ssid(void);
```
Fetches the STA SSID from the C5 via `SPI_ID_WIFI_GET_STA_INFO` into a static
buffer; returns it (NUL-terminated) or `NULL` on failure.

#### `wifi_service_change_to_hotspot`
```c
void wifi_service_change_to_hotspot(const char *new_ssid);
```
On the P4 this is just a wrapper around `wifi_service_set_ap_ssid(new_ssid)`.

### Promiscuous Mode

#### `wifi_service_promiscuous_start` / `wifi_service_promiscuous_stop`
```c
void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter);
void wifi_service_promiscuous_stop(void);
```
Forward `SPI_ID_WIFI_PROMISC_START` / `SPI_ID_WIFI_PROMISC_STOP`. The `cb` and
`filter` arguments are **ignored** on the P4 (the capture and its callback run on
the C5); the signature is kept for source compatibility. If the C5 replies
`ESP_ERR_NOT_SUPPORTED`, a warning is logged.

### Channel Hopping

#### `wifi_service_start_channel_hopping` / `wifi_service_stop_channel_hopping`
```c
void wifi_service_start_channel_hopping(void);
void wifi_service_stop_channel_hopping(void);
```
Forward `SPI_ID_WIFI_CH_HOP_START` / `SPI_ID_WIFI_CH_HOP_STOP`. The hopping task
itself lives on the C5.

### Configuration Storage

All setters forward to the C5, which persists to `WIFI_AP_CONFIG_FILE` and
applies any radio state change.

#### `wifi_service_save_ap_config`
```c
esp_err_t wifi_service_save_ap_config(
    const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled);
```
Packs the fields into `spi_wifi_ap_config_t` and forwards
`SPI_ID_WIFI_SAVE_AP_CONFIG`. Returns `ESP_ERR_INVALID_ARG` if `ssid` is NULL.

#### Individual Setters
```c
esp_err_t wifi_service_set_enabled(bool enabled);       // SPI_ID_WIFI_SET_ENABLED
esp_err_t wifi_service_set_ap_ssid(const char *ssid);   // SPI_ID_WIFI_SET_AP
esp_err_t wifi_service_set_ap_password(const char *p);  // SPI_ID_WIFI_SET_AP_PASSWORD
esp_err_t wifi_service_set_ap_max_conn(uint8_t n);      // SPI_ID_WIFI_SET_AP_MAX_CONN
esp_err_t wifi_service_set_ap_ip(const char *ip_addr);  // SPI_ID_WIFI_SET_AP_IP
```
Each forwards one SPI command. `wifi_service_set_ap_password` /
`wifi_service_set_ap_ip` return `ESP_ERR_INVALID_ARG` on a NULL argument.

---

# C5

`firmware_c5/components/Service/wifi/` - this is where the **real Wi-Fi stack
lives**. It manages AP mode, STA mode, scanning, promiscuous capture, channel
hopping, and JSON config persistence directly on the esp-idf Wi-Fi driver.

## Functionality Overview

- **Initialization/Deinitialization:** NVS, Netif, default event loop + handlers,
  and the Wi-Fi driver, brought up in `WIFI_MODE_APSTA`.
- **Access Point (AP):** Configurable SSID, password, max connections, custom
  static IP (default `192.168.4.1`), DHCP server.
- **Scanning:** Active scan storing up to `WIFI_SCAN_LIST_SIZE` results.
- **Station (STA):** Joins external networks.
- **Promiscuous Mode / Channel Hopping:** Low-level capture plus a background task
  cycling channels for environment monitoring.
- **Configuration Persistence:** AP settings to/from `WIFI_AP_CONFIG_FILE`
  (`config/wifi/wifi_ap.conf`) via `storage_assets`.
- **Known Networks:** Connected credentials saved to `WIFI_KNOWN_NETWORKS_FILE`
  (`storage/wifi/know_networks.json`).

## API Functions

### Initialization & Lifecycle

#### `wifi_service_init`
```c
void wifi_service_init(void);
```
Initializes the Wi-Fi stack in `APSTA` mode: NVS (erase if needed), default event
loop + handlers, AP config load (defaults `Darth Maul` / `MyPassword123`), static
IP and DHCP server. If the loaded config has `enabled == false`, the driver is
initialized but the radio is **not** started.

#### `wifi_service_deinit`
```c
void wifi_service_deinit(void);
```
Fully shuts down the service: stops the driver, unregisters handlers, deinits the
driver, frees mutexes, and clears static state.

#### `wifi_service_start` / `wifi_service_stop`
```c
void wifi_service_start(void);
void wifi_service_stop(void);
```
Start or stop the driver without full deinit. `wifi_service_stop` also clears
stored scan results.

#### `wifi_service_is_busy`
```c
bool wifi_service_is_busy(void);
```
Returns `true` while a capture (promiscuous sniffer) is running.

#### `wifi_service_set_power_save`
```c
void wifi_service_set_power_save(bool deep);
```
Sets the Wi-Fi modem-sleep depth via `esp_wifi_set_ps`: `deep == true` selects
`WIFI_PS_MAX_MODEM`, otherwise `WIFI_PS_MIN_MODEM`.

### Scanning

#### `wifi_service_scan`
```c
void wifi_service_scan(void);
```
Performs an active Wi-Fi scan (mutex-guarded, stops channel hopping first). Stores
up to `WIFI_SCAN_LIST_SIZE` results and gives LED feedback (red on failure, blue
on success).

#### `wifi_service_get_ap_count`
```c
uint16_t wifi_service_get_ap_count(void);
```
Returns the number of networks found in the last scan.

#### `wifi_service_get_ap_record`
```c
wifi_ap_record_t *wifi_service_get_ap_record(uint16_t index);
```
Returns a pointer to a specific scan result, or `NULL` if the index is invalid.

### Connection & Management

#### `wifi_service_connect_to_ap`
```c
esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password);
```
Connects (as STA) to an external AP.
- Auth mode chosen by password presence (WPA2_PSK or OPEN).
- Disconnects any existing connection first.
- **Persistence:** saves SSID/password to `WIFI_KNOWN_NETWORKS_FILE`, updating the
  password if the network already exists.

#### `wifi_service_is_connected`
```c
bool wifi_service_is_connected(void);
```
Returns `true` if connected to an external network and holding an IP. This is
**the C5's own STA connection**, set by `IP_EVENT_STA_GOT_IP` and cleared by
`WIFI_EVENT_STA_DISCONNECTED`. It says nothing about a monitored AP restarting
or another client's reconnection. Capture observations must come from received
frames for the relevant BSSID/station, independently of this getter.

#### `wifi_service_is_active`
```c
bool wifi_service_is_active(void);
```
Returns `true` if the service is started (driver up, interface up).

#### `wifi_service_get_connected_ssid`
```c
const char *wifi_service_get_connected_ssid(void);
```
Returns the connected SSID, or `NULL` if not connected.

#### `wifi_service_change_to_hotspot`
```c
void wifi_service_change_to_hotspot(const char *new_ssid);
```
Reconfigures the AP to an **Open** network with the given SSID: briefly stops the
driver, sets `authmode` to `WIFI_AUTH_OPEN`, restarts with the new config.

### Promiscuous Mode

#### `wifi_service_promiscuous_start`
```c
void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter);
```
Enables promiscuous (sniffer) mode with the given packet callback and filter mask
(e.g. `WIFI_PROMIS_FILTER_MASK_MGMT`; `NULL` for no filter).

#### `wifi_service_promiscuous_stop`
```c
void wifi_service_promiscuous_stop(void);
```
Disables promiscuous mode and clears the callback.

### Channel Hopping

#### `wifi_service_start_channel_hopping`
```c
void wifi_service_start_channel_hopping(void);
```
Starts a background task cycling channels 1..13 (250 ms cadence via
`esp_wifi_set_channel`), useful for promiscuous applications (e.g. deauth
detection). Both the task stack and TCB are allocated in **PSRAM** (`SPIRAM`) to
spare internal RAM.

#### `wifi_service_stop_channel_hopping`
```c
void wifi_service_stop_channel_hopping(void);
```
Stops the channel-hopping task and frees its resources.

### Configuration Storage

#### `wifi_service_save_ap_config`
```c
esp_err_t wifi_service_save_ap_config(
    const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled);
```
Serializes the settings with `cJSON` and writes them to `WIFI_AP_CONFIG_FILE`.
**State management:** if `enabled` is `true` and Wi-Fi is inactive it calls
`wifi_service_start()`; if `enabled` is `false` and Wi-Fi is active it calls
`wifi_service_stop()`.

#### Individual Setters
Helpers that update a single parameter while preserving the rest, auto-saving and
triggering state changes when `enabled` toggles.
```c
esp_err_t wifi_service_set_enabled(bool enabled);
esp_err_t wifi_service_set_ap_ssid(const char *ssid);
esp_err_t wifi_service_set_ap_password(const char *password);
esp_err_t wifi_service_set_ap_max_conn(uint8_t max_conn);
esp_err_t wifi_service_set_ap_ip(const char *ip_addr);
```

**Internal loader:** an internal `load_ap_config` runs during init. If `enabled`
is `false` in the config, `wifi_service_init` brings the driver up but does **not**
start the radio.

## Internal Implementation Details

### Event Handling
A static `event_handler` (registered for `WIFI_EVENT` and `IP_EVENT`) manages
station connect/disconnect and IP-assignment events, logging and driving LED
feedback (green on connect / IP, red on disconnect).

### Thread Safety
A mutex protects the scan path (`wifi_service_scan`) against concurrent scan
requests.

### Channel Hopping Task
Runs as a static FreeRTOS task; stack and TCB are placed in **PSRAM** (`SPIRAM`)
to preserve internal RAM.

### Memory & String Handling
`cJSON` is used for config parse/serialize; SSIDs/passwords are copied with
`strncpy` plus explicit NUL-termination.
</content>
</invoke>

## Integrated dual-band analysis

The integrated C5 enables automatic 2.4/5 GHz band selection and provides
`wifi_service_scan_ex()` for explicit band, passive/active mode and dwell times.
P4 survey screens use SPI proxy results rather than local radio access. Scan band
selection is scoped to the scan and restored afterward. Packet analysis supports
band-constrained capture and counter-only monitors.

C5 channel-hopping and sniffer stream tasks are pinned to `SYS_CORE_MAIN`;
P4 proxy/UI workers use the central `sys_prio.h` policy. Availability of bands and
channels still depends on the configured radio and region. Source/host validation
does not establish physical reception or packet-capture reliability.

### Handshake capture and lifecycle

Capture timing belongs to the C5 Wi-Fi service; received frames feed observations
from the C5 sniffer. The P4 owns the proxy/session interface and presentation.
Do not open P4 SPI client sessions from C5 application code. The manual console
command uses the existing asynchronous deauther session API, reports start
failure through its return code, and makes no claim about a client's reconnection.
It is separate from the lab capture window and its per-boot budget.
Verbose sniffer diagnostics use `ESP_LOGD` instead of per-frame stdout/flush.

The P4 handshake screen allocates its polling task stack in PSRAM, preserving
internal DMA memory for peripherals. Capture-start failure and task-allocation
failure are shown on screen instead of leaving a false capturing state. Back
and programmatic navigation request cancellation through the registered close
hook; deletion is a fallback. Updates carry a generation token so a closed
session cannot update a later screen. Re-entry while the previous worker is
still stopping reports that state and keeps Back available instead of reviving
the old worker. The worker stops the capture it successfully started before
releasing its busy state. These paths require hardware acceptance under sustained
capture and repeated entry/exit; host tests do not validate RF capture quality.

The handshake screen starts the EAPOL filter with SD persistence. Default EAPOL
and PMKID captures use one file per BSSID in `/sdcard/wifi/loot/handshakes/`:
`<sanitized-SSID>_<12-digit-BSSID>.pcap`. ASCII letters, digits, `_` and `-` are
preserved; other SSID bytes become `_` in the filename only. Original SSID bytes
remain in the captured frame. Same-name APs have separate files. Existing files
for a BSSID are found by suffix, checked for a compatible header and complete
records, and appended across capture sessions. Invalid/truncated files are
reported as errors, never overwritten. If a known SSID changes, the established
filename is retained; newly observed metadata is appended on a subsequent session.

No file is created for an AP until an EAPOL frame arrives. A frame arriving before
SSID identification creates `hidden_<BSSID>.pcap`; a later identifying frame is
appended and the file is renamed without replacing an existing destination.
Until that identification arrives, conversion may lack the required SSID. APs
with no EAPOL traffic only occupy the bounded metadata cache and create no files.
The P4 cache has 32 entries in PSRAM, with replacement; eviction does not delete
files. At most one file is open while appending. Each append checks write/close
results. The per-AP PCAP uses raw 802.11 link type 105 with the received FCS removed.

C5 also forwards beacons, probe responses and association/reassociation requests
in these filters. Beacon and probe-response forwarding is limited separately to
one of each per AP every five seconds, within the existing 128-AP table. P4 caches
an identifying frame and writes it with that AP's EAPOL traffic. This preserves
SSID/BSSID metadata without saving every surrounding beacon. No probe injection
or deauthentication is performed by this path. Both chips must be updated.

An explicitly opened capture file retains the single-file radiotap path used by
console callers; its header must be written and flushed before radio start.
The UI worker refreshes C5 statistics and checks storage status every second.
Statistics, session or storage failures stop capture and keep Back available.
Default per-AP capture copies each complete SPI frame into a bounded 64-slot
PSRAM queue (2048 bytes per frame, about 132 KiB plus an 8 KiB writer stack).
A separate P4 service task serializes SD writes and close operations. The SPI
callback never waits for SD or queue space. PCAP timestamps represent P4 frame
receipt, including cached AP metadata, rather than delayed disk writes.
Queue overflow or contention drops the new frame and increments a visible loss
counter; it never overwrites a frame being written. The first loss also emits a
warning log/LED. SD failure latches an error and discards remaining queued frames,
counted separately. Pending/peak/lost/failed counters are shown in the storage card.
Stopping closes admission, drains accepted frames, and only then frees queue and
AP-cache memory. BACK shows “Finishing SD writes...” until this completes. The
explicit-file radiotap path remains synchronous. `wifi_sniffer_flush_capture()`
checks the per-AP writer status; it is not a queue-drain barrier. Stale session
callbacks are rejected before enqueueing. A write failure is latched; no later records are appended to a damaged
stream. Successful close/flush does not guarantee survival of power loss or that
every frame observed on C5 arrived at P4. The public capture-path getter returns
the directory for per-AP mode; `AP capture:` and `AP identified:` logs show files.

The handshake screen refreshes every second with the channel, received-packet
count, successfully saved EAPOL count/bytes, and the eight most recently updated
AP files (newest first). UP/DOWN scrolls within chrome bounds in both orientations;
BACK stops capture, waits for pending SD writes, then exits. Files remain on SD after leaving the screen.
The list uses the sanitized filename/SSID plus BSSID so equal names remain distinct;
unknown names show `hidden` and a name-pending status, updated after SSID discovery.
Counters cover this session, not pre-existing PCAP contents. The bounded recent
list is not a full SD inventory. First saved EAPOL for an AP outside that list emits
`EAPOL saved:` and the semantic INFO LED (purple by default); repeated packets for
listed APs update counters without repeated flashes. Metadata alone or failed
writes never signal capture success. Snapshots are copied under the capture mutex,
then published to LVGL through a generation-checked asynchronous update. Capture
errors remain visible with BACK usable. C5 observations are displayed separately
from persisted P4 records; saving EAPOL alone does not prove a complete handshake.

The EAPOL-pair observation accepts **M1+M2, M2+M3 or M3+M4**; PMKID remains
an independent capture condition. It means an adjacent pair was observed on C5,
not four-way completion, MIC verification or proof of a usable saved PCAP.
M3+M4 alone may lack the SNonce needed by an offline verifier; preserve the other
EAPOL records and validate the exported file separately.

Matching requires the same AP/station, key-descriptor type and key version,
with each message no older than ten seconds. M1/M2 and M3/M4 require equal replay
counters; M2/M3 requires the latter to be exactly one greater, checked across the
full eight-byte counter without accepting overflow. Skipped replay values from
retransmissions are deliberately not guessed. Messages can arrive in either
order. Up to sixteen AP/station sessions retain separate timestamps and counters
for each message; stale observations expire and a full table replaces a session.
M1/M2/M3 require a nonzero nonce. M4 requires a zero nonce, no key data and the
Secure flag for RSN; legacy WPA can omit Secure. Group-key, error/request and SMK
frames are excluded. Frame/header bounds, LLC/SNAP, EAPOL-Key type, descriptor,
declared lengths and AP/client direction are checked before classification.
The conventional 16-byte-MIC WPA/RSN layout is used; this is not a classifier for
all WPA3/Enterprise variants (such as the 24-byte-MIC layout). Valid EAPOL
records with unsupported layouts remain in the PCAP without being classified. PMKID detection
accepts a bounded plaintext KDE or RSN PMKID list. Encrypted key data is not parsed.
The detector does not transmit probe requests from the Wi-Fi callback.

A bridge-streaming C5 session does not allocate or fill a duplicate 200 KiB PCAP
buffer; a counter-only monitor also needs no PCAP buffer. The paired SPI v2 sniffer statistics append lab-attempt/failure counters and
remaining hold time; both firmware applications must be updated. Host tests `test_handshake_parser.py`, `test_sniffer_pcap.py` and
`test_handshake_lifecycle.py` exercise all three pairs in either order, isolation,
replay carry/overflow, expiry/clock wrap, malformed PMKID data, storage
failure and cancellation with ASan/UBSan. They do not measure RF reception or
validate the saved file against a real AP exchange. The EAPOL field definitions
follow the pinned SDK's `wpa_supplicant/src/common/wpa_common.h`.
`test_ap_capture.py` executes the production writer with synthetic frames and real
host files: AP isolation, duplicate names, late SSID discovery, restart/append,
filename sanitization and damaged-file rejection. When `tshark` is available it
also checks that the generated PCAP exposes EAPOL and identifying management
frames. This does not establish Hashcat conversion or RF capture success on hardware.

For acceptance, start Handshake with an SD card, reconnect a client to an owned
WPA2 test AP, exit with Back and inspect the logged per-AP PCAP. Check its SSID,
BSSID and EAPOL messages in Wireshark, then verify conversion with hcxpcapngtool.
Repeat entry/exit to confirm the same AP file grows; test a second BSSID with the
same SSID to confirm separation. Inspect errors rather than relying on the C5
EAPOL-pair observation as proof of saved records.

The UI uses hopping; the existing sniffer API can select a fixed channel, which
is not a BSSID filter. For EAPOL/PMKID hopping only, C5 now uses a deterministic
passive dwell policy: 120 ms for an unobserved channel, at least 200 ms after a
beacon/probe response, 750 ms after observed authentication/association, and
1000 ms after a structurally bounded EAPOL frame. Repeated activity extends the
visit up to a hard 2000 ms cap, except for the bounded lab AP observation
window described below, then the channel-selection policy runs again. Timing is
checked every 20 ms (plus task scheduling/channel-switch overhead). Other hopper
users retain a 250 ms dwell. Band lists are unchanged: dual-band visits 22 channels;
the empty-channel nominal cycle falls from 5.5 s to 2.64 s, while activity extends
it. These are configured timing estimates, not measured capture performance.

Observations only update synchronized timing state, never perform radio/I/O calls
inside the receive callback. Events from a different channel or after a committed
hop are ignored. Stop clears the policy so later tools do not inherit a hold.
`test_capture_hopping.py` exercises production timing with a fake clock, including
hold caps, stale observations and reset. C5 must be updated to use this policy;
the lab action/status extension also requires the matching P4. Test repeated phone reconnection with a
saved password against a known AP and compare with fixed-channel reception.

Source comparison (2026-11-13): [Bruce sniffer](https://github.com/BruceDevices/firmware/blob/main/src/modules/wifi/sniffer.cpp)
uses a separate writer queue and exposes manual channel selection in its sniffer
loop; the presence of a `HOP_INTERVAL` define alone does not prove its use.
[Pwnagotchi agent](https://github.com/evilsocket/pwnagotchi/blob/master/pwnagotchi/agent.py)
groups APs by channel, orders populated channels first and controls dwell via
Bettercap. Its waits also relate to active operations, so its results cannot be
attributed to ML or hopping alone. These sources informed the comparison; the Gutem handshake adaptation is
attributed separately below.

Capture hopping uses recent activity to select revisits as described below. The default per-AP P4 path
uses the writer queue described above; C5 stream queues can still drop frames
when full. P4 loss counters do not account for losses before SPI frame assembly.
Host tests (`test_capture_queue.py`, `test_ap_capture.py`) verify bounded storage,
copy ownership, FIFO order, receipt timestamps, concurrent drain, SD failure and
restart. These mocks do not establish capture performance on hardware. A broad scan listens on one channel
at a time and can still miss an entire exchange on another channel; holding a
channel helps only after activity is actually observed.


### Capture memory placement

C5 AP metadata, handshake sessions, channel scores and AP activity histories use
explicit `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` allocations. Capacities remain
128 known APs, 16 handshake sessions, 32 channels and 64 activity entries. The
four tables occupy 6,464 bytes on C5, excluding allocator overhead. They are
allocated in task context before capture starts, with partial allocations freed
on failure and no fallback to internal RAM. START fails if preparation fails.
The bounded allocations are retained for reuse across stops/restarts, avoiding
freeing storage that late RX callbacks could still reference; session histories
are cleared on restart. Locks and the per-boot BSSID attempt/captured ledger stay
in internal RAM. Linker headroom is a static budget, not measured runtime heap.

### Recent-activity channel selection and Gutem attribution

For EAPOL/PMKID capture on C5, the hopper first visits every channel in the selected
band plan. Subsequent visits use a bounded, deterministic recent-activity score:

- A distinct BSSID contributes one AP point at most once per 30 seconds. Beacon
  and probe-response frames share that allowance; repeated beacons do not inflate
  it. BSSID must be nonzero/unicast, and beacon source must match BSSID.
- Association/authentication contributes four points, and a structurally validated
  EAPOL-Key contributes eight. Each class is limited to once per BSSID/channel per
  second. These are observations, not proof of a complete or persisted handshake.
- Each channel score saturates at 64 and halves every 30 seconds. Time since the
  previous visit adds up to 20 points; ties prefer the older visit.
- Unvisited channels take precedence. Once the oldest visited channel has waited
  five seconds, it takes precedence over scores. Five seconds is a revisit trigger,
  not a hard RF latency guarantee: current dwell and other overdue channels still
  take time. Without a lab AP observation window, the nominal conservative bound is five
  seconds plus two seconds per channel, excluding scheduling/radio overhead.
  Lab AP windows can add up to 15 seconds to a visit and delay exploration. Initial exploration and channel-change failures also consume time.
- The selected channel may remain the same if it wins again. The existing
  association/EAPOL dwell extension remains; empty channels still use 120 ms.

The capture scheduler accepts beacon/probe-response observations only when their
complete IE list contains a structurally valid RSN or WPA element. It validates
version, selector-list counts, optional capabilities and RSN PMKID-list bounds;
omitted trailing fields use protocol defaults, as in the pinned SDK parser.
Empty elements, partial fields, truncated trailing IEs and duplicate RSN/WPA
elements are rejected. This is structural validation, not proof that an advertised
cipher is supported, that a handshake can be obtained, or that TX will succeed.
Open networks with neither RSN nor WPA are excluded. The current implementation
has no SSID-name filter and does not consult the client table.

A matching M1+M2, M2+M3 or M3+M4 observation, or detected PMKID marks its BSSID independently of the
single-result UI flags. Both PMKID KDE and RSN-list paths mark observations, even
when another AP has already produced the session's first badge. The shared
64-entry boot ledger also reserves a record for an AP observed before its first
attempt; a record with zero attempts but an observation is occupied, not reusable.
Repeated marks are idempotent. A full ledger never evicts a record; untracked APs
cannot receive new attempts while it remains full.

Marking an AP cancels its not-yet-claimed pending action without touching another
AP's action or shortening an existing listening window. An action already claimed
by the worker cannot be recalled. The observation and attempt ledger survives
capture stop/restart until C5 reboot. **Observed on C5 is not saved on P4:** the
suppression flag does not acknowledge SD persistence, MIC verification or a full
four-message handshake. Loss on SPI or SD failure can still leave no usable PCAP;
consult P4 saved-record/error indicators separately.

C5 reception only schedules a bounded action. The channel-hopper task invokes a
registered application callback after releasing the observation lock. Service
owns timing/budgets; Applications supplies the checked one-frame radio adapter.
There is no Service-to-Applications include or P4 session API on C5. The adapter
checks the actual channel, does not retune it, returns the SDK TX result, and does
not start the continuous deauther worker or bypass the SDK's frame validation.
A rejected transmission remains a failure, not a successful deauthentication.

Each visit schedules at most one AP action and then listens for **15 seconds from
the TX call's completion**, including failed calls. Repeated frames cannot extend
the window. After expiry the ordinary channel selector resumes. A later attempt
requires another identifying frame on a subsequent visit; three transmissions
are not fired in a tight loop. Each BSSID has at most **three reserved attempts
per C5 boot**, across channels, stop/restart and band changes. Cancellation of a
pending action still consumes its reservation. The static 64-BSSID ledger never
evicts; when full, additional APs remain passive until reboot.

Stopping synchronizes with an in-flight TX call, cancels pending work and clears
the hold before deleting the hopper and freeing its stack. Fixed-channel capture
and non-EAPOL/PMKID modes do not schedule lab actions. The sniffer's EAPOL/PMKID
hopping modes enable this scheduler; starting Handshake enables this behavior.
No inference is made from `wifi_service_is_connected()` or from a successful TX
return about AP availability, client reconnection or handshake completeness.

The P4 screen shows remaining hold seconds, last per-AP attempt
number, total attempted TX calls and failures for this session. INFO means the
C5 driver accepted a TX call; warnings mean it rejected one. Packet persistence
and EAPOL-pair/PMKID indicators retain their separate meanings. Both SPI header copies
append the same six-byte status tail within the existing payload limit; no new
operation is allocated. Install matching P4/C5 apps for the status display.

History contains at most 32 channel records and 64 BSSID/channel records. Full AP
history replaces the least recently observed entry, so deduplication is bounded,
not a lifetime inventory. Channel scores remain capped even under cache churn.
Only observations on the current visit update history. Stop and band-plan changes
clear it; no NVS/SD persistence, allocations or radio calls occur in the observation
callback. Non-capture tools retain round-robin hopping and their existing dwell.

Credit for the scheduling ideas (decaying activity, weighted selection, and
periodic exploration) goes to **Gutem and contributors, pwnagotchi-esp32**:
[radio scheduler at revision 976a8afab0745693e0d9693dd1e72bf9e9a7f700](https://github.com/Gutem/pwnagotchi-esp32/blob/976a8afab0745693e0d9693dd1e72bf9e9a7f700/firmware/pwnagotchi-esp32/radio_scheduler.cpp).
Its [NOTICE](https://github.com/Gutem/pwnagotchi-esp32/blob/976a8afab0745693e0d9693dd1e72bf9e9a7f700/NOTICE)
credits Copyright (C) 2026 Gutem and contributors and specifies GPL-3.0-only.
The Highboy scheduler uses BSSID deduplication and oldest-visit deadlines
instead of the upstream 12-work-choice exploration interval, retains Highboy's
short dwell policy, and has no ML dependency. The scoped lab TX workflow above
supersedes the previous passive-only AP/client hold.

Handshake pair recognition in C5 `wifi_sniffer.c` is adapted from Gutem's
[`capture_model.cpp` (`hs_complete`)](https://github.com/Gutem/pwnagotchi-esp32/blob/976a8afab0745693e0d9693dd1e72bf9e9a7f700/firmware/pwnagotchi-esp32/capture_model.cpp).
The upstream copyright is Copyright (C) 2026 Gutem and contributors; the
upstream-derived logic retains GPL-3.0-only terms, without the "or later" option.
Its notice is preserved immediately above `track_handshake`, alongside the
HIGH CODE adaptation notice dated 2026-11-14. Local changes integrate the logic
into C with AP/client correlation, replay and descriptor/key-version checks,
and per-message expiration. The revision linked here is the verified reference;
the original copy revision has not been established. The project's first-party
header does not replace the license terms of the upstream-derived logic.

`tools/highboy/tests/test_capture_hopping.py` runs the production policy with a
controlled clock and sanitizers, checking initial survey, score preference,
deduplication, aging, saturation, bounded revisits, stale events, band changes,
stop/restart, legacy dwell, RSN/WPA bounds, malformed IE tails, per-BSSID
observations before/after attempts, pending-action cancellation, deferred TX, TX failure,
15-second expiry, per-BSSID boot budgets, invalid MACs and ledger exhaustion. This is host validation, not RF
performance evidence.
The lab capture update requires matching C5 and P4 applications. The P4 writer
queue remains responsible for SD persistence. Hardware acceptance compares
repeated reconnections on an owned AP against fixed-channel capture, then repeats
with APs on different channels; inspect saved EAPOL frames and P4 queue losses.
