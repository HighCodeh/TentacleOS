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

# Connection settings

P4 `Service/connection_settings` owns asynchronous changes to the C5 radios and
persistence of the user's preferences. Controls live at **Settings → Connections
→ Wi-Fi / Bluetooth**. The quick panel no longer contains radio switches.

## Controls

| Menu | Settings |
| --- | --- |
| Wi-Fi | Radio on/off; network name (1–32 bytes); hotspot password (empty for open, otherwise 8–63 bytes); maximum 1–8 clients; Connect to network |
| Bluetooth | Radio on/off; device name (1–29 bytes); maximum 1–3 clients; Visible advertising on/off |

Values are drafts until **Save & apply**. BACK discards unapplied edits. The
password editor masks its input; the list only shows a mask or Open network.
Connect to network opens the existing AP discovery/connection screen after edits
have been applied or discarded. The Wi-Fi name/password/client limit refer to
the Highboy's own access point, not credentials for a selected external network.
Selecting a known secured network reuses its saved password, including after a
radio off/on cycle. New networks open the keyboard; failed connections offer
password editing. Credentials are retained on C5 only after a successful join
with an IP. See [saved-network connections](../wifi/README.md#connection--management).

The BLE limit applies to the generic connectable advertiser and is bounded by
the C5 NimBLE build limit. HID keeps its existing single-peer connection and
pairing behavior; it uses the configured device name. Dedicated application
profiles retain their own connection/security policies. No Bluetooth PIN option
or pairing-policy change is introduced. The 29-byte name fits in the legacy
scan-response packet including its AD header.

## Ownership and failures

The UI owns its draft, keyboard and LVGL refresh timer. Both screen IDs register
a close hook; DELETE is an idempotent fallback. BACK remains available during
application or failure. The Service owns the copied request and worker, so it
can complete after navigation without retaining or calling a screen object.

One request runs at a time. Request data and the worker stack use explicit PSRAM;
allocation/task failure releases ownership and reports failure. Snapshot updates
use an internal critical-section lock. SPI, radio lifecycle and filesystem work
run outside LVGL. Results have a revision so a screen can consume completion.

Wi-Fi settings validate and apply on C5 before persistence. The driver is stopped
when necessary, AP configuration/IP are applied, and radio state is restored to
the requested on/off value. Failure attempts to restore the previous AP/IP and
running state; rollback failures are logged. An active scan, application session
or promiscuous capture rejects a conflicting settings request.

Bluetooth applies deinit, checked config save, start if enabled, and advertising
if enabled and visible. Lifecycle failures remain retryable; resources retained
by a timed-out SDK stop are not freed prematurely. See the
[Bluetooth lifecycle](../bluetooth/README.md#initialization--lifecycle).

P4 updates and saves the canonical config only after successful radio application.
A failed P4 SD save leaves the applied in-memory values intact and reports
**Applied; save failed**. Retry Save & apply to persist them. Radio/application
failures report failure without saving a new boot preference. Success/error
results pair an ESP log with the configured LED signal.

## Persistence and boot

P4 `config/wifi.conf` and `config/ble.conf` on SD hold user preferences. Wi-Fi
uses its existing AP/client config structure; BLE stores `name`, `max_conn`,
`advertising`, `enabled` and `enabled_by_user`. Legacy implicit BLE-on files load
as off until an explicit save. The Wi-Fi reader also accepts the flat factory
AP config; the BLE reader accepts its legacy `ssid` name field.

P4 boot applies the loaded Wi-Fi config and BLE announcement config to C5. BLE
only starts when explicitly enabled. Entering the BLE application menu does not
change that preference; applications can acquire BLE on demand. Existing Wi-Fi
application-menu auto-enable behavior remains unchanged.

Background config sync skips the two radio configs already applied by boot,
preventing factory assets from overwriting user settings. Explicit config-sync
pushes serialize the current P4 radio preferences. C5 config writes check write,
flush and close before replacing the previous file.

## Validation and deployment

Host regressions (not hardware qualification):

- `python3 tools/bluetooth/test_connection_settings.py`: request allocation/task
  failures, overlap, radio errors, SD-save errors, on/off and visibility behavior.
- `python3 tools/bluetooth/test_connection_backend.py`: C5 live AP changes,
  full-length SSID, client/auth settings, rollback and interrupted file writes.
- `python3 tools/bluetooth/test_announce_settings.py`: generic BLE connection
  cap, packet-size budget, advertisement errors and configured HID name.
- `python3 tools/highboy/tests/test_connection_settings_ui.py <keyboard-test-dir>`:
  real LVGL menus in 240×320/320×240 and two themes, drafts, masking, failed apply,
  failed save/retry, background completion and screen re-entry. First run
  `test_keyboard_ui.py` with the same directory to build host LVGL.

Build/update both P4 and C5 applications. No new assets or wire-format changes
are required. Hardware validation remains necessary for on/off across reboot,
real AP connections with the new password/client limit, BLE visibility and HID
pairing, and a failed/retried operation over the physical SPI bridge.
