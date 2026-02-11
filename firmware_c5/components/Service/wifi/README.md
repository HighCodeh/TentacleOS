# Wi-Fi Service Component Documentation

This component manages Wi-Fi functionalities including Access Point (AP) mode, Station (STA) mode, scanning, and configuration persistence using JSON files.

## Functionality Overview

The service handles:
- **Initialization/Deinitialization:** Setup of NVS, Netif, Event Loops, and Wi-Fi drivers.
- **Access Point (AP):** Configurable SSID, password, max connections, and custom IP address.
- **Scanning:** Active scanning for nearby networks.
- **Station (STA):** Connecting to external Wi-Fi networks.
- **Hotspot Management:** Dynamic switching of AP configuration.
- **Promiscuous Mode:** Low-level packet sniffing and environment monitoring.
- **Channel Hopping:** Automated cycling through Wi-Fi channels for environment monitoring.
- **Configuration Persistence:** Loading and saving AP settings to/from `assets/config/wifi/wifi_ap.conf`.
- **Known Networks:** Automatically saves connected network credentials to `assets/storage/wifi/know_networks.json`.

## API Functions

### Initialization & Lifecycle

#### `wifi_init`
```c
void wifi_init(void);
```
Initializes the Wi-Fi stack in `APSTA` mode.
- Initializes NVS (performing erase if necessary).
- Sets up the default event loop and registers handlers.
- Loads AP configuration from storage (or uses defaults "Darth Maul"/"MyPassword123").
- Configures the static IP (default: 192.168.4.1) and starts the DHCP server.

#### `wifi_deinit`
```c
void wifi_deinit(void);
```
Completely shuts down the Wi-Fi service.
- Stops the Wi-Fi driver.
- Unregisters event handlers.
- Deinitializes the driver.
- Frees synchronization primitives (mutexes) and clears static data.

#### `wifi_start` / `wifi_stop`
```c
void wifi_start(void);
void wifi_stop(void);
```
Simple wrappers to start or stop the Wi-Fi driver without full deinitialization. `wifi_stop` also clears stored scan results.

### Scanning

#### `wifi_service_scan`
```c
void wifi_service_scan(void);
```
Performs an active Wi-Fi scan.
- Uses a mutex to ensure thread safety.
- Stores up to `WIFI_SCAN_LIST_SIZE` results internally.
- Provides visual feedback via LEDs (Green for AP connection, Red for failures, Blue for scan success).

#### `wifi_service_get_ap_count`
```c
uint16_t wifi_service_get_ap_count(void);
```
Returns the number of networks found in the last scan.

#### `wifi_service_get_ap_record`
```c
wifi_ap_record_t* wifi_service_get_ap_record(uint16_t index);
```
Retrieves a pointer to a specific scan result record. Returns `NULL` if the index is invalid.

### Connection & Management

#### `wifi_service_connect_to_ap`
```c
esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password);
```
Connects the device (as a station) to an external Access Point.
- Configures authentication mode based on the presence of a password (WPA2_PSK or OPEN).
- Disconnects any existing connection before attempting a new one.
- **Persistence:** Automatically saves the SSID and password to `assets/storage/wifi/know_networks.json`. If the network already exists, the password is updated.

#### `wifi_service_is_connected`
```c
bool wifi_service_is_connected(void);
```
Returns `true` if the device is currently connected to an external Wi-Fi network and has an IP address.

#### `wifi_service_is_active`
```c
bool wifi_service_is_active(void);
```
Returns `true` if the Wi-Fi service is started (driver initialized and interface up).

#### `wifi_service_get_connected_ssid`
```c
const char* wifi_service_get_connected_ssid(void);
```
Returns the SSID of the currently connected network. Returns `NULL` if not connected.

#### `wifi_change_to_hotspot`
```c
void wifi_change_to_hotspot(const char *new_ssid);
```
Dynamically reconfigures the device's Access Point to an **Open** network with the specified SSID.
- Stops the Wi-Fi driver briefly to apply changes.
- Sets `authmode` to `WIFI_AUTH_OPEN`.
- Restarts Wi-Fi with the new configuration.

### Promiscuous Mode

#### `wifi_service_promiscuous_start`
```c
void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter);
```
Enables promiscuous mode (sniffer) with a custom callback and filter.
- `cb`: Function to handle captured packets.
- `filter`: Filter mask (e.g., `WIFI_PROMIS_FILTER_MASK_MGMT`).

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
Starts a background task that cycles the Wi-Fi interface through channels 1 to 13. 
- Useful for promiscuous mode applications (e.g., deauth detection).
- Task memory is allocated in PSRAM if available.

#### `wifi_service_stop_channel_hopping`
```c
void wifi_service_stop_channel_hopping(void);
```
Stops the channel hopping task and frees associated memory resources.

### Configuration Storage

#### `wifi_save_ap_config`
```c
esp_err_t wifi_save_ap_config(const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled);
```
Saves the AP configuration to a JSON file (`/assets/config/wifi/wifi_ap.conf`).
- Uses `cJSON` to serialize settings.
- Persists data using the storage API.
- **State Management:** If `enabled` is `true` and Wi-Fi is inactive, it calls `wifi_start()`. If `enabled` is `false` and Wi-Fi is active, it calls `wifi_stop()`.

#### Individual Setters
Helper functions to update a single configuration parameter while preserving others. They automatically save the config and trigger state changes if `enabled` is toggled.

```c
esp_err_t wifi_set_wifi_enabled(bool enabled);
esp_err_t wifi_set_ap_ssid(const char *ssid);
esp_err_t wifi_set_ap_password(const char *password);
esp_err_t wifi_set_ap_max_conn(uint8_t max_conn);
esp_err_t wifi_set_ap_ip(const char *ip_addr);
```

**Internal Loader:** `wifi_load_ap_config` is called during initialization to read these settings. If `enabled` is found to be `false` in the config, `wifi_init` will initialize the driver but **not** start the radio.

## Internal Implementation Details

### Event Handling
A static `wifi_event_handler` manages Wi-Fi and IP events:
- **WIFI_EVENT_AP_STACONNECTED:** Logs the MAC of the connected station and blinks Green.
- **WIFI_EVENT_AP_STADISCONNECTED:** Blinks Red.
- **IP_EVENT_AP_STAIPASSIGNED:** Logs IP assignment and blinks Green.

### Thread Safety
A `wifi_mutex` (Semaphore) is used to protect the scanning process (`wifi_service_scan`), preventing concurrent scan requests which could lead to resource conflicts.

### Channel Hopping Task
The channel hopping feature runs as a static FreeRTOS task. It uses `esp_wifi_set_channel` to switch channels every 250ms. To optimize internal RAM usage, both the task stack and the Task Control Block (TCB) are allocated in **PSRAM** using the `SPIRAM` capability.

### Castings & Memory Management
- **cJSON:** Used extensively for parsing and generating configuration files.
- **PSRAM Allocation:** Critical tasks and large buffers are allocated in PSRAM to preserve internal memory.
- **Type Casting:** `event_data` is cast to specific event structures (e.g., `wifi_event_ap_staconnected_t*`) within handlers.
- **String Handling:** `strncpy` is used safely with explicit null-termination to prevent buffer overflows when handling SSIDs and passwords.
