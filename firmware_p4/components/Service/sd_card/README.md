# SD Directory Management Component

Component for managing directories on SD card storage.

## Overview

- **Location:** `components/storage/sd_dir/`
- **Main Header:** `include/sd_dir.h`
- **Dependencies:** `esp_vfs`, `esp_vfs_fat`, `sdmmc`, `storage_sd`

## Key Features

- **Directory Operations:** Create, delete, list, and check existence
- **Recursive Operations:** Remove trees, copy directories, calculate sizes
- **Predefined Paths:** System-wide constants for organizing data
- **Callback System:** Efficient iteration with custom callbacks
- **Statistics:** Count files/directories, calculate storage usage

## Path Constants

All path constants have been centralized in `tos_storage_paths.h` using `TOS_PATH_*` macros.
The sd_card component uses `VFS_MOUNT_POINT` (from `vfs_config.h`) as the mount point prefix.

See `storage_api/include/tos_storage_paths.h` for the full list of available paths.

## API Reference

### Directory Creation & Deletion

#### `sd_dir_create`
```c
esp_err_t sd_dir_create(const char *path);
```
Creates directory with automatic parent creation (like `mkdir -p`).

**Returns:** `ESP_OK` on success, `ESP_FAIL` on failure.

---

#### `sd_dir_remove_recursive`
```c
esp_err_t sd_dir_remove_recursive(const char *path);
```
Recursively deletes directory and all contents. **Use with caution.**

**Returns:** `ESP_OK` on success, `ESP_FAIL` on failure.

---

### Directory Information

#### `sd_dir_exists`
```c
bool sd_dir_exists(const char *path);
```
Checks if directory exists.

**Returns:** `true` if exists, `false` otherwise.

---

#### `sd_dir_list`
```c
typedef void (*sd_dir_callback_t)(const char *name, bool is_dir, void *user_data);
esp_err_t sd_dir_list(const char *path, sd_dir_callback_t callback, void *user_data);
```
Iterates through directory entries, calling callback for each item.

**Example:**
```c
void print_entry(const char *name, bool is_dir, void *user_data) {
    printf("%s %s\n", is_dir ? "[DIR]" : "[FILE]", name);
}
sd_dir_list("/sdcard/badusb", print_entry, NULL);
```

---

#### `sd_dir_count`
```c
esp_err_t sd_dir_count(const char *path, uint32_t *file_count, uint32_t *dir_count);
```
Counts files and subdirectories (non-recursive).

**Returns:** `ESP_OK` on success.

---

#### `sd_dir_get_size`
```c
esp_err_t sd_dir_get_size(const char *path, uint64_t *total_size);
```
Calculates total size of all files in directory tree (recursive).

**Returns:** `ESP_OK` on success.

---

### Directory Operations

#### `sd_dir_copy_recursive`
```c
esp_err_t sd_dir_copy_recursive(const char *src, const char *dst);
```
Copies entire directory tree, preserving structure.

**Returns:** `ESP_OK` on success.

---

## Implementation Details

- All functions require full paths including `VFS_MOUNT_POINT`
- Functions are not thread-safe - use mutexes for concurrent access
- Recursive operations may fail on deeply nested directories

## Usage Example

```c
#include "tos_storage_paths.h"

void example(void) {
    sd_dir_create(TOS_PATH_NFC);
    sd_dir_create(TOS_PATH_BADUSB);
}
```

---

# SD Card Information Component

Component for querying SD card hardware and filesystem statistics.

## Overview

- **Location:** `components/storage/sd_card_info/`
- **Main Header:** `include/sd_card_info.h`
- **Dependencies:** `esp_vfs_fat`, `sdmmc_cmd`, `ff`, `storage_sd`

## Key Features

- **Hardware Info:** Card name, capacity, speed, type
- **Filesystem Stats:** Total, used, free space with percentages
- **Mount Status:** Check if card is accessible
- **Debug Output:** Console logging of card information

## Data Structures

### `sd_card_info_t`
```c
typedef struct {
    char name[16];           // Card manufacturer name
    uint32_t capacity_mb;    // Total capacity in MB
    uint32_t sector_size;    // Sector size in bytes
    uint32_t num_sectors;    // Total number of sectors
    uint32_t speed_khz;      // Max speed in kHz
    uint8_t card_type;       // Card type identifier
    bool is_mounted;         // Mount status
} sd_card_info_t;
```

### `sd_fs_stats_t`
```c
typedef struct {
    uint64_t total_bytes;    // Total capacity
    uint64_t used_bytes;     // Space in use
    uint64_t free_bytes;     // Available space
} sd_fs_stats_t;
```

## API Reference

### Card Information

#### `sd_get_card_info`
```c
esp_err_t sd_get_card_info(sd_card_info_t *info);
```
Retrieves complete hardware information.

**Returns:** `ESP_OK`, `ESP_ERR_INVALID_STATE`, or `ESP_ERR_INVALID_ARG`.

---

#### `sd_print_card_info`
```c
void sd_print_card_info(void);
```
Prints formatted card information to console.

---

### Filesystem Statistics

#### `sd_get_fs_stats`
```c
esp_err_t sd_get_fs_stats(sd_fs_stats_t *stats);
```
Retrieves complete filesystem statistics.

**Returns:** `ESP_OK`, `ESP_ERR_INVALID_STATE`, `ESP_ERR_INVALID_ARG`, or `ESP_FAIL`.

---

#### `sd_get_free_space`
```c
esp_err_t sd_get_free_space(uint64_t *free_bytes);
```
Gets available free space.

---

#### `sd_get_total_space`
```c
esp_err_t sd_get_total_space(uint64_t *total_bytes);
```
Gets total filesystem capacity.

---

#### `sd_get_used_space`
```c
esp_err_t sd_get_used_space(uint64_t *used_bytes);
```
Gets space currently in use.

---

#### `sd_get_usage_percent`
```c
esp_err_t sd_get_usage_percent(float *percentage);
```
Calculates usage percentage (0.0 to 100.0).

---

### Individual Attributes

#### `sd_get_card_name`
```c
esp_err_t sd_get_card_name(char *name, size_t size);
```
Gets manufacturer name.

---

#### `sd_get_capacity`
```c
esp_err_t sd_get_capacity(uint32_t *capacity_mb);
```
Gets total capacity in MB.

---

#### `sd_get_speed`
```c
esp_err_t sd_get_speed(uint32_t *speed_khz);
```
Gets maximum communication speed.

---

#### `sd_get_card_type`
```c
esp_err_t sd_get_card_type(uint8_t *type);
```
Gets raw card type identifier.

---

#### `sd_get_card_type_name`
```c
esp_err_t sd_get_card_type_name(char *type_name, size_t size);
```
Gets human-readable card type string.

---

## Implementation Details

- Uses FatFS `f_getfree()` for filesystem stats
- Accesses SDMMC layer for hardware information
- All functions verify mount status before access
- Thread-safe for read operations

## Usage Example

```c
void check_storage_health(void) {
    sd_card_info_t info;
    float usage;
    
    if (sd_get_card_info(&info) == ESP_OK && 
        sd_get_usage_percent(&usage) == ESP_OK) {
        
        printf("Card: %s (%lu MB)\n", info.name, info.capacity_mb);
        printf("Usage: %.1f%%\n", usage);
        
        if (usage > 90.0f) {
            printf("WARNING: Low disk space!\n");
        }
    }
}
```

---

# SD Card Initialization Component

Component for SD card initialization, mounting, and lifecycle management.

## Overview

- **Location:** `components/storage/sd_card_init/`
- **Main Header:** `include/sd_card_init.h`
- **Dependencies:** `esp_vfs_fat`, `driver/sdspi_host`, `sdmmc_cmd`, `spi`, `pin_def`

## Key Features

- **Simple Initialization:** One-function setup with defaults
- **Custom Configuration:** Control max files, auto-format, allocation size
- **Mount Management:** Mount, unmount, remount, check status
- **Shared SPI Bus:** Integration with centralized SPI driver
- **Health Monitoring:** Basic health checks
- **Card Handle Access:** Low-level SDMMC handle for advanced use

## Configuration

```c
// VFS_MOUNT_POINT is defined in vfs_config.h (e.g. "/sdcard")
#define SD_MAX_FILES 10                    // Max open files
#define SD_ALLOCATION_UNIT 16 * 1024       // 16KB cluster size
```

## API Reference

### Initialization

#### `sd_init`
```c
esp_err_t sd_init(void);
```
Initializes SD card with default settings.

**Returns:** `ESP_OK`, `ESP_ERR_INVALID_STATE`, or `ESP_FAIL`.

---

#### `sd_init_custom`
```c
esp_err_t sd_init_custom(uint8_t max_files, bool format_if_failed);
```
Initializes with custom parameters.

**Warning:** `format_if_failed=true` erases all data on mount failure.

---

#### `sd_init_custom_pins`
```c
esp_err_t sd_init_custom_pins(int mosi, int miso, int clk, int cs);
```
**Deprecated:** Custom pins not supported with shared SPI driver.

---

### Deinitialization

#### `sd_deinit`
```c
esp_err_t sd_deinit(void);
```
Unmounts SD card and releases resources. Close all files first.

**Returns:** `ESP_OK`, `ESP_ERR_INVALID_STATE`, or `ESP_FAIL`.

---

### Status & Maintenance

#### `sd_is_mounted`
```c
bool sd_is_mounted(void);
```
Checks if SD card is mounted.

---

#### `sd_remount`
```c
esp_err_t sd_remount(void);
```
Unmounts and remounts SD card (useful for error recovery).

---

#### `sd_check_health`
```c
esp_err_t sd_check_health(void);
```
Performs basic health check.

---

#### `sd_reset_bus`
```c
esp_err_t sd_reset_bus(void);
```
**Not Supported:** Returns `ESP_ERR_NOT_SUPPORTED`. Use `sd_remount()` instead.

---

### Advanced Access

#### `sd_get_card_handle`
```c
sdmmc_card_t* sd_get_card_handle(void);
```
Returns pointer to internal SDMMC card structure. Returns `NULL` if not mounted.

**Warning:** Direct manipulation can interfere with VFS operations.

---

## Implementation Details

### SPI Configuration
```c
spi_device_config_t sd_cfg = {
    .cs_pin = SD_CARD_CS_PIN,
    .clock_speed_hz = 20000 * 1000,
    .mode = 0,
    .queue_size = 4,
};
```

### Mount Configuration
```c
esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024,
};
```

## Troubleshooting

| Problem | Solutions |
|---------|-----------|
| `sd_init()` returns `ESP_FAIL` | Check card insertion, verify pins, try different card, enable debug logs |
| File operations fail | Check filesystem corruption, verify max_files limit, close file handles, try remount |
| Random disconnects | Check power supply, verify connections, reduce clock speed, add pull-ups |
| `sd_deinit()` fails | Close all file handles first, check for active tasks |

## Usage Example

```c
void storage_init(void) {
    if (sd_init() == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted");
        sd_dir_create("/sdcard/config");
    } else {
        ESP_LOGE(TAG, "SD card mount failed");
    }
}
```

---

# SD Card Read Component

Component for comprehensive SD card file reading operations.

## Overview

- **Location:** `components/storage/sd_card_read/`
- **Main Header:** `include/sd_card_read.h`
- **Dependencies:** `esp_vfs_fat`, `storage_sd`

## Key Features

- **Text Reading:** Entire files, specific lines, line-by-line processing
- **Binary Reading:** Raw data, chunks, individual bytes
- **Type Conversion:** Direct reading of integers, floats
- **Content Search:** String search and occurrence counting
- **Flexible Paths:** Automatic `/sdcard` prefix for relative paths

## Configuration

```c
#define MAX_PATH_LEN 256    // Maximum path length
#define MAX_LINE_LEN 512    // Maximum line length
```

## API Reference

### Text Reading

#### `sd_read_string`
```c
esp_err_t sd_read_string(const char *path, char *buffer, size_t buffer_size);
```
Reads entire file as null-terminated string.

---

#### `sd_read_line`
```c
esp_err_t sd_read_line(const char *path, char *buffer, size_t buffer_size, uint32_t line_number);
```
Reads specific line (1-based index).

---

#### `sd_read_first_line`
```c
esp_err_t sd_read_first_line(const char *path, char *buffer, size_t buffer_size);
```
Reads first line. Equivalent to `sd_read_line(path, buffer, size, 1)`.

---

#### `sd_read_last_line`
```c
esp_err_t sd_read_last_line(const char *path, char *buffer, size_t buffer_size);
```
Reads last line.

---

#### `sd_read_lines`
```c
typedef void (*sd_line_callback_t)(const char *line, void *user_data);
esp_err_t sd_read_lines(const char *path, sd_line_callback_t callback, void *user_data);
```
Processes each line via callback. Memory-efficient for large files.

---

#### `sd_count_lines`
```c
esp_err_t sd_count_lines(const char *path, uint32_t *line_count);
```
Counts total lines in file.

---

### Binary Reading

#### `sd_read_binary`
```c
esp_err_t sd_read_binary(const char *path, void *buffer, size_t size, size_t *bytes_read);
```
Reads raw binary data.

---

#### `sd_read_chunk`
```c
esp_err_t sd_read_chunk(const char *path, size_t offset, void *buffer, size_t size, size_t *bytes_read);
```
Reads data chunk from specific offset.

---

#### `sd_read_bytes`
```c
esp_err_t sd_read_bytes(const char *path, uint8_t *bytes, size_t max_count, size_t *count);
```
Alias for `sd_read_binary` with byte array typing.

---

#### `sd_read_byte`
```c
esp_err_t sd_read_byte(const char *path, uint8_t *byte);
```
Reads single byte.

---

### Type Conversion

#### `sd_read_int`
```c
esp_err_t sd_read_int(const char *path, int32_t *value);
```
Reads and converts to 32-bit integer.

---

#### `sd_read_float`
```c
esp_err_t sd_read_float(const char *path, float *value);
```
Reads and converts to float.

---

### Content Search

#### `sd_file_contains`
```c
esp_err_t sd_file_contains(const char *path, const char *search, bool *found);
```
Checks if string exists in file.

---

#### `sd_count_occurrences`
```c
esp_err_t sd_count_occurrences(const char *path, const char *search, uint32_t *count);
```
Counts string occurrences in file.

---

## Implementation Details

- Line functions allocate 512-byte stack buffers
- Use `sd_read_lines()` callback for large files
- Thread-safe for different files
- Automatic path formatting (relative → absolute)

## Usage Example

```c
void process_config(void) {
    char buffer[256];
    
    // Read entire file
    if (sd_read_string("/config/settings.txt", buffer, sizeof(buffer)) == ESP_OK) {
        printf("Config: %s\n", buffer);
    }
    
    // Process line-by-line
    sd_read_lines("/logs/system.log", [](const char *line, void *ctx) {
        printf("Log: %s\n", line);
    }, NULL);
}
```

---

# SD Card Write Component

Component for comprehensive SD card file writing operations.

## Overview

- **Location:** `components/storage/sd_card_write/`
- **Main Header:** `include/sd_card_write.h`
- **Dependencies:** `esp_vfs_fat`, `storage_sd`

## Key Features

- **Text Writing:** Strings, lines, formatted text
- **Binary Writing:** Raw data, buffers, individual bytes
- **Append Operations:** Add to existing files
- **Formatted Output:** Printf-style writing
- **CSV Support:** Simplified row writing

## API Reference

### Text Writing

#### `sd_write_string` / `sd_append_string`
```c
esp_err_t sd_write_string(const char *path, const char *data);
esp_err_t sd_append_string(const char *path, const char *data);
```
Writes or appends string.

---

#### `sd_write_line` / `sd_append_line`
```c
esp_err_t sd_write_line(const char *path, const char *line);
esp_err_t sd_append_line(const char *path, const char *line);
```
Writes or appends line with automatic newline.

---

#### `sd_write_formatted` / `sd_append_formatted`
```c
esp_err_t sd_write_formatted(const char *path, const char *format, ...);
esp_err_t sd_append_formatted(const char *path, const char *format, ...);
```
Printf-style formatted writing.

---

### Binary Writing

#### `sd_write_binary` / `sd_append_binary`
```c
esp_err_t sd_write_binary(const char *path, const void *data, size_t size);
esp_err_t sd_append_binary(const char *path, const void *data, size_t size);
```
Writes or appends binary data.

---

#### `sd_write_buffer`
```c
esp_err_t sd_write_buffer(const char *path, const void *buffer, size_t size);
```
Alias for `sd_write_binary`.

---

#### `sd_write_bytes`
```c
esp_err_t sd_write_bytes(const char *path, const uint8_t *bytes, size_t count);
```
Writes byte array.

---

#### `sd_write_byte`
```c
esp_err_t sd_write_byte(const char *path, uint8_t byte);
```
Writes single byte.

---

### Type Helpers

#### `sd_write_int`
```c
esp_err_t sd_write_int(const char *path, int32_t value);
```
Writes integer as decimal text.

---

#### `sd_write_float`
```c
esp_err_t sd_write_float(const char *path, float value);
```
Writes float with 6 decimal places.

---

### CSV Support

#### `sd_write_csv_row` / `sd_append_csv_row`
```c
esp_err_t sd_write_csv_row(const char *path, const char **columns, size_t num_columns);
esp_err_t sd_append_csv_row(const char *path, const char **columns, size_t num_columns);
```
Writes or appends CSV row (comma-separated with newline).

---

## Implementation Details

- All writes verify byte count matches expected size
- Automatic `/sdcard` prefix for relative paths
- Buffers flushed automatically on file close

## Usage Example

```c
void log_event(const char *type, const char *msg) {
    time_t now = time(NULL);
    sd_append_formatted("/logs/events.log", "[%ld] %s: %s\n", now, type, msg);
}

void save_sensor_data(float temp, float humidity) {
    const char *row[] = {
        "Temperature", "Humidity"
    };
    sd_write_csv_row("/data/sensors.csv", row, 2);
    
    char temp_str[16], hum_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.2f", temp);
    snprintf(hum_str, sizeof(hum_str), "%.2f", humidity);
    
    const char *data[] = {temp_str, hum_str};
    sd_append_csv_row("/data/sensors.csv", data, 2);
}
```

---

# SD Card File Management Component

Component for comprehensive SD card file operations.

## Overview

- **Location:** `components/storage/sd_card_file/`
- **Main Header:** `include/sd_card_file.h`
- **Dependencies:** `esp_vfs`, `esp_vfs_fat`, `sdmmc`, `storage_sd`

## Key Features

- **File Operations:** Create, delete, rename, move, copy
- **Metadata Access:** Size, modification time, attributes
- **File Comparison:** Byte-by-byte comparison
- **File Truncation:** Resize to specific length
- **Utilities:** Check existence, get extensions, clear contents

## Data Structures

### `sd_file_info_t`
```c
typedef struct {
    char path[256];        // Full path
    size_t size;           // File size in bytes
    time_t modified_time;  // Last modification time
    bool is_directory;     // Directory flag
} sd_file_info_t;
```

## API Reference

### File Information

#### `sd_file_exists`
```c
bool sd_file_exists(const char *path);
```
Checks if file exists.

---

#### `sd_file_get_info`
```c
esp_err_t sd_file_get_info(const char *path, sd_file_info_t *info);
```
Retrieves complete file information.

---

#### `sd_file_get_size`
```c
esp_err_t sd_file_get_size(const char *path, size_t *size);
```
Gets file size in bytes.

---

#### `sd_file_is_empty`
```c
esp_err_t sd_file_is_empty(const char *path, bool *is_empty);
```
Checks if file has zero bytes.

---

### File Manipulation

#### `sd_file_delete`
```c
esp_err_t sd_file_delete(const char *path);
```
Permanently deletes file.

---

#### `sd_file_rename`
```c
esp_err_t sd_file_rename(const char *old_path, const char *new_path);
```
Renames or moves file (same filesystem).

---

#### `sd_file_move`
```c
esp_err_t sd_file_move(const char *src_path, const char *dst_path);
```
Moves file (alias for rename).

---

#### `sd_file_copy`
```c
esp_err_t sd_file_copy(const char *src_path, const char *dst_path);
```
Copies file (source unchanged).

---

#### `sd_file_truncate`
```c
esp_err_t sd_file_truncate(const char *path, size_t size);
```
Resizes file to specified size.

---

#### `sd_file_clear`
```c
esp_err_t sd_file_clear(const char *path);
```
Clears all content (makes empty).

---

### File Comparison

#### `sd_file_compare`
```c
esp_err_t sd_file_compare(const char *path1, const char *path2, bool *are_equal);
```
Byte-by-byte comparison.

---

### Utilities

#### `sd_file_get_extension`
```c
esp_err_t sd_file_get_extension(const char *path, char *extension, size_t size);
```
Extracts file extension (without dot).

---

## Implementation Details

- Rename/move are atomic, copy is not
- Path buffer in `sd_file_info_t` is 256 bytes
- Not thread-safe - use mutexes for concurrent access

## Usage Example

```c
esp_err_t backup_config(void) {
    const char *config = "/sdcard/config/settings.json";
    const char *backup = "/sdcard/backups/settings.json";
    
    // Create backup
    if (sd_file_copy(config, backup) != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Verify backup
    bool equal;
    sd_file_compare(config, backup, &equal);
    
    return equal ? ESP_OK : ESP_FAIL;
}
```