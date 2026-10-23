<!--
SPDX-License-Identifier: GPL-3.0-or-later

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

# ESP SCAN — AP scan and heuristic priority

For Highboy / High Boy, **Wi-Fi > ESP SCAN** now reuses the existing normal
Wi-Fi AP scan. It does not invoke the dedicated monitor-mode Quick Scan backend,
stop/restart normal Wi-Fi, hop channels itself, or repeat the scan to fill a
30/45/60-second window. **Iniciar scan** triggers one normal scan; its actual
runtime depends on the C5 driver and channel plan.

## Architecture

- P4 `Applications/wifi/wifi_scan_view.c` runs a background radio worker.
- It calls the existing `spi_bridge_run_scan(SPI_ID_WIFI_APP_SCAN_AP,
  SPI_ID_WIFI_SCAN_STATUS, NULL, 0)` used by the Wi-Fi applications.
- C5's existing `scan_fn_app_ap()` uses `wifi_service_scan()`, then publishes
  compact `spi_wifi_scan_record_t` AP records through the shared data pipe.
- P4 validates count/record response lengths, copies BSSID/SSID/channel/RSSI,
  reuses the Espressif entries of its existing `mac_vendor_get_name()` table,
  and sorts by priority. A compact P4 build option omits other manufacturers
  from this lookup to fit the original app slot; those names remain unknown.
  The full table remains available when that build option is disabled.
- `ui_wifi_esp_scan.c` renders the synchronized local records. LVGL never waits
  on scan/SPI calls. It shows heuristic %, MAC/BSSID, SSID, OUI, manufacturer and
  RSSI together on a dedicated result page.

This uses the existing scan path and protocol IDs; no new AP discovery pass or
new firmware protocol is required. The C5 remains the radio owner. If Wi-Fi is
already active, this screen does not deliberately disconnect it or switch to
NULL mode. The normal service may start Wi-Fi when it was disabled, as it does
for the existing AP scan. Scanning still shares radio airtime with normal use.

## Priority and evidence

| Available evidence | Points |
| --- | ---: |
| Global MAC whose OUI lookup identifies Espressif | 69 |
| Current AP scan RSSI at least -55 dBm | 31 |
| Current AP scan RSSI from -65 to -56 dBm | 16 |
| ESP-NOW/frame fingerprint | 0 (not observed) |
| Temporal persistence or signal consistency | 0 (not observed) |

Score is the sum of the available OUI and RSSI points, at most **100/100**.
The two available criteria now occupy the full scale, approximately preserving
their original 45:20 relative weights. The medium RSSI tier earns about half of
the signal weight (16 points); below -65 dBm it earns zero. A recognized Espressif
AP therefore scores 100 with strong RSSI, 85 with medium RSSI and 69 with weak
RSSI. An unknown/local MAC can earn at most 31 from RSSI alone.
The `%` display is a heuristic priority index, not the probability of spying,
a microphone, recording or even a confirmed Espressif chipset. The single RSSI
sample indicates signal strength; it does not establish consistency or distance.
MAC/OUI can be spoofed, and the offline manufacturer database is incomplete.
Locally administered/randomized MACs receive no Espressif OUI points even if
their prefix resembles an entry. No raw frame count, ESP-NOW evidence or
persistence samples are invented from AP scan records.

The offline P4/C5 tables now include `10:BD:A3`, which the earlier table omitted.
It is listed as Espressif in the
[Nmap manufacturer database](https://raw.githubusercontent.com/nmap/nmap/master/nmap-mac-prefixes)
(checked 2026-10-20), and appeared in the supplied C5 AP log as
`10:BD:A3:C0:8C:B9`. For this global prefix, a strong AP sample now earns 69 OUI +
31 RSSI = 100 points, rather than only the 31 RSSI points. This does not identify
a network by SSID: `Darth Maul` is also a configurable/default SSID, and a
different or locally administered BSSID using that name earns no OUI points
from its name. The reported network's actual BSSID is needed to diagnose any
other missing prefix; the table remains partial, not a complete registry.

The **ESP** filter selects APs with Espressif OUI evidence. **Todos**
shows the full returned AP list, including other vendors. An empty SSID is shown
as **Rede oculta**. SSIDs are bounded and sanitized before display.
This mode does not load an authorization registry or apply the separate capture
backend's RAM whitelist; therefore no authorized/unknown whitelist distinction
is offered in the screen filters.

## Screen flow and controls

Only ESP SCAN receives a black background; no shared theme is changed. Its ready
page has an **INICIAR SCAN** button. OK starts the existing AP scan and switches
to the shared `waves_ui` animation. Completion opens a full-screen result instead
of appending a list below the button.

Each result shows its SSID above a circular indicator filled according to the
0–100 score, with a larger percentage centered inside. MAC, OUI, manufacturer
and RSSI appear below with colored emphasis. The top counter identifies the
current result and filter. Empty filters show **Sem redes no filtro**.

- LEFT/RIGHT browse results; DOWN also advances.
- UP toggles ESP/Todos on the ready or result page, using cached records.
- BACK from a result returns to the start page; BACK there returns to Wi-Fi.
- BACK during scanning requests cancellation; another BACK can leave the screen
  while the existing worker finishes cancellation.

Changing result or filter does not trigger another scan. Screen cleanup removes
the refresh timer and the animation's LVGL objects. Layout uses the current
display dimensions when the page is built.

## Scope and cancellation

This detects **access points advertising Wi-Fi networks**. An ESP operating only
as a connected station, an unobserved/unsupported channel, or a Wi-Fi-disabled
device can be absent. The ordinary scan does not enumerate every Wi-Fi client.
It uses the normal active AP scan and may transmit probe requests; it does not
send deauth or authenticate/connect to candidates for this classification.

Cancel/BACK requests local cancellation. The existing shared scan helper remains
in charge of the in-flight scan until it completes or times out; the UI does not
stop the radio to abort it. Results are discarded on cancellation before fetching,
or retained as a partial local list if cancellation occurs while reading records.
A second UI worker cannot start until the previous worker exits. Generic scan
results share the existing C5 data pipe with other applications: another client
must not replace that result set while records are being read. The normal scan
status reports completion, not a separate driver-error payload; consult C5
`WIFI_SERVICE` logs when the existing scan itself fails.

`wifi_scan_view_start(seconds)` retains the accepted legacy presets 30/45/60 for
source compatibility, but the parameter no longer schedules a timed capture.
The UI no longer offers duration selection. Errors from scan RPCs and malformed
count/record responses are exposed to the UI, rather than treated as empty
successful results.

## Legacy capture backend

The separate C5 `wifi_quick_scan_*` API, parser/classifier, RAM whitelist and
scoped `wifi_service_capture_passive()` remain available in source for dedicated
frame-capture callers. **ESP SCAN no longer calls them.** Their stricter ownership
rules and frame-derived scoring are separate from the AP snapshot above. Previous
monitor-mode initialization failures do not describe the new screen path.
The paired v8 headers remain identical. Existing bounded, unique, CRC-checked
byte-aligned reply recovery now covers the normal AP scan, scan-status and generic
data replies used here; corrupt, ambiguous and truncated replies remain rejected.

## Verification

```sh
python3 tools/wifi_quick_scan/test_quick_scan.py
python3 tools/highboy/test_preservation.py
python3 tools/highboy/check_contracts.py
bash tools/build.sh p4
```

The UI-worker regression test runs the production P4 worker and asserts exactly
one existing AP scan and no STOP/START/Quick Scan RPCs. It covers sorting,
Espressif/local-MAC classification, RSSI scoring, hidden SSIDs, scan/data errors,
malformed responses, cancellation, empty results and worker creation failure.
The other ASan/UBSan groups still exercise the legacy capture core, proxy,
controller, driver cleanup and SPI response recovery. They do not prove physical
RF reception or full device behavior. This UI routing change needs only the
updated P4 app when the C5 already supports the existing normal AP-scan commands.

The AP-based P4 build passed on IDF v5.5.3 at **2,948,880 / 2,949,120 bytes**
(240 bytes free). All six sanitizer test groups, all 34 preservation tests and
the paired-contract checks passed. No partition was enlarged and no hardware
scan or flash was performed during this routing change.

The two-criterion 100-point scaling passed the six sanitizer groups, including
RSSI boundaries at -55/-56/-65/-66 dBm, unknown/local-MAC exclusion and the
100-point maximum. The P4 app rebuild passed the original slot-size check;
no device flash or physical score acceptance was performed for this adjustment.

The fullscreen UI revision passed the P4 build and original slot-size check at
**2,949,104 / 2,949,120 bytes** (16 bytes free). A host render using the production
screen, shared button/waves components and LVGL exercised start, animation,
100%/85% result navigation and BACK at 240×320 and 320×240. These renders use fake
scan snapshots; they do not validate physical controls, radio or display timing.
The scan backend and scoring weights are unchanged by this UI revision.
