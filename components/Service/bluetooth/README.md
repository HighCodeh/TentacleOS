# Bluetooth Service Component Documentation

This component manages the Bluetooth Low Energy (BLE) functionality of the Highboy device using the NimBLE stack. It handles initialization, advertising, power management, and persistent configuration for both standard device announcements and specific "spam" lists.

## Overview

- **Location:** `components/Service/bluetooth/`
- **Main Header:** `include/bluetooth_service.h`
- **Stack:** Apache NimBLE (via `nimble_port`)
- **Dependencies:** `nvs_flash`, `storage_assets`, `cJSON`

## API Functions

### Initialization & Lifecycle

#### `bluetooth_service_init`
```c
esp_err_t bluetooth_service_init(void);
```
Initializes the BLE stack (NimBLE).
- Initializes NVS (wiping if necessary).
- Initializes the NimBLE port.
- Sets callbacks for reset (`on_reset`), sync (`on_sync`), and storage (`ble_store_util_status_rr`).
- Loads the device name from `ble_announce.conf` (default: "Darth Maul").
- Spawns a FreeRTOS task (`bluetooth_service_host_task`) to run the NimBLE host.
- Waits (up to 10s) for the stack to sync using a semaphore.

#### `bluetooth_service_stop`
```c
esp_err_t bluetooth_service_stop(void);
```
Stops the BLE service, halts the NimBLE port, and cleans up resources (semaphores, flags).

### Advertising Management

#### `bluetooth_service_start_advertising`
```c
esp_err_t bluetooth_service_start_advertising(void);
```
Starts standard connectable advertising.
- Configures advertisement fields (Flags: `DISC_GEN` | `BREDR_UNSUP`, TX Power: Auto, Device Name).
- Sets UUID 16-bit Service (Device Information Service).
- Automatically restarts advertising on disconnect events.

#### `bluetooth_service_stop_advertising`
```c
esp_err_t bluetooth_service_stop_advertising(void);
```
Stops any active advertising.

### Power Management

#### `bluetooth_service_set_max_power`
```c
esp_err_t bluetooth_service_set_max_power(void);
```
Sets the Bluetooth transmission power to the maximum supported level (`ESP_PWR_LVL_P9` / +9dBm) for both Advertising and Default connection types.

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

### Task & Synchronization
- **`bluetooth_service_host_task`:** Runs `nimble_port_run()`, which processes the NimBLE event queue.
- **`ble_hs_synced_sem`:** A binary semaphore used during initialization to block `bluetooth_service_init` until the host stack has fully synchronized with the controller (`bluetooth_service_on_sync`).

### Event Handling
- **`bluetooth_service_gap_event`:** Handles GAP events:
    - `BLE_GAP_EVENT_CONNECT`: Logs connection and restarts advertising if failed.
    - `BLE_GAP_EVENT_DISCONNECT`: Logs disconnection and restarts advertising.
    - `BLE_GAP_EVENT_ADV_COMPLETE`: Restarts advertising when one cycle completes.

### Configuration Files
1.  **Announce Config:** `assets/config/bluetooth/ble_announce.conf`
    -   JSON: `{"ssid": "Name", "max_conn": 4}`
2.  **Spam/Beacon List:** `assets/config/bluetooth/beacon_list.conf`
    -   JSON: `{"spam_ble_announce": ["Device 1", "Device 2", ...]}`
