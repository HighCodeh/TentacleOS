# Bluetooth Service Component Documentation

This component manages the Bluetooth Low Energy (BLE) functionality of the device using the Apache NimBLE stack. It provides a high-level API for initialization, lifecycle management, scanning, advertising, connection handling, and address randomization.

## Overview

- **Location:** `components/Service/bluetooth/`
- **Main Header:** `include/bluetooth_service.h`
- **Stack:** Apache NimBLE (via `nimble_port`)
- **Dependencies:** `nvs_flash`, `storage_assets`, `cJSON`, `esp_random`

## API Functions

### Initialization & Lifecycle

The service lifecycle is split into initialization (resource allocation) and start (execution).

#### `bluetooth_service_init`
```c
esp_err_t bluetooth_service_init(void);
```
Allocates resources and prepares the BLE stack.
- Initializes NVS.
- Initializes the NimBLE port.
- Configures GAP callbacks and loads persistent device configuration.
- Does **not** start the background task.

#### `bluetooth_service_start`
```c
esp_err_t bluetooth_service_start(void);
```
Spawns the NimBLE host task and waits (up to 10s) for the controller to synchronize.

#### `bluetooth_service_stop`
```c
esp_err_t bluetooth_service_stop(void);
```
Stops the NimBLE host task. The service is "paused", but resources remain allocated in memory.

#### `bluetooth_service_deinit`
```c
esp_err_t bluetooth_service_deinit(void);
```
Completely shuts down the stack and frees all allocated memory and semaphores.

#### `Status Checks`
- `bluetooth_service_is_initialized()`: Returns `true` if resources are allocated.
- `bluetooth_service_is_running()`: Returns `true` if the host task is active.

### Scanning

#### `bluetooth_service_scan`
```c
void bluetooth_service_scan(uint32_t duration_ms);
```
Performs a blocking discovery procedure for the specified duration. Results are stored in an internal cache.

#### `Scan Results`
- `bluetooth_service_get_scan_count()`: Returns the number of unique devices found.
- `bluetooth_service_get_scan_result(uint16_t index)`: Returns a pointer to a `ble_scan_result_t` structure containing name, RSSI, and MAC address.

### Advertising Management

#### `bluetooth_service_start_advertising` / `stop_advertising`
Standard connectable advertising using the configured device name. Advertising automatically restarts on disconnection.

### Connection Management

#### `bluetooth_service_disconnect_all`
```c
void bluetooth_service_disconnect_all(void);
```
Terminates all active GAP connections.

#### `bluetooth_service_get_connected_count`
```c
int bluetooth_service_get_connected_count(void);
```
Returns the number of currently connected peers (tracked internally).

### Address Management

#### `bluetooth_service_get_mac`
```c
void bluetooth_service_get_mac(uint8_t *mac);
```
Copies the 6-byte current identity address into the provided buffer.

#### `bluetooth_service_get_own_addr_type`
```c
uint8_t bluetooth_service_get_own_addr_type(void);
```
Returns the current address type (e.g., Public, Random Static) used by the stack.

#### `bluetooth_service_set_random_mac`
```c
esp_err_t bluetooth_service_set_random_mac(void);
```
Generates and sets a new **Random Static Address**. This stops active advertising and switches the address type to `BLE_OWN_ADDR_RANDOM`.

### Power Management

#### `bluetooth_service_set_max_power`
Sets TX power to `ESP_PWR_LVL_P9` (+9dBm) for advertising and connections.

### Configuration & Persistence

#### `bluetooth_save_announce_config`
```c
esp_err_t bluetooth_save_announce_config(const char *name, uint8_t max_conn);
```
Saves the main device announcement settings (Device Name) to `/assets/config/bluetooth/ble_announce.conf`.

#### `bluetooth_load_spam_list`
```c
esp_err_t bluetooth_load_spam_list(char ***list, size_t *count);
```
Loads a list of beacon names/payloads from `/assets/config/bluetooth/beacon_list.conf` used for specific application logic (e.g., spam functions).
- **Memory:** Allocates an array of strings. The caller **must** free this memory using `bluetooth_free_spam_list`.

#### `bluetooth_save_spam_list`
```c
esp_err_t bluetooth_save_spam_list(const char * const *list, size_t count);
```
Saves a list of strings to the beacon configuration file.

#### `bluetooth_free_spam_list`
```c
void bluetooth_free_spam_list(char **list, size_t count);
```
Helper function to safely free the memory allocated by `bluetooth_load_spam_list`.

## Internal Implementation Details

### Connection Tracking
The service maintains an internal array (`connection_handles`) of active peers. This is updated via `BLE_GAP_EVENT_CONNECT` and `BLE_GAP_EVENT_DISCONNECT` in the GAP event handler to allow mass disconnection and status reporting without relying on private NimBLE headers.

### Event Handling
- `BLE_GAP_EVENT_DISC`: Parsed advertisement data to populate the scan results cache.
- `BLE_GAP_EVENT_DISC_COMPLETE`: Signals the completion of the scan via a semaphore.
- `BLE_GAP_EVENT_CONNECT/DISCONNECT`: Logs events and manages the connection tracking list.

### Configuration Files
- `assets/config/bluetooth/ble_announce.conf`: Device name and connection limits.
- `assets/config/bluetooth/beacon_list.conf`: Payload list for BLE spam functions.
