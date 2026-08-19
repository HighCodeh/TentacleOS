# Wi-Fi service

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
Returns `true` if connected to an external network and holding an IP.

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
