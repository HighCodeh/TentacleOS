# Storage Assets Component

This component provides read-only access to a dedicated LittleFS partition for storing static application assets like images, fonts, configuration files, and other resources that are flashed with the firmware.

## Overview

- **Location:** `components/storage/storage_assets/`
- **Main Header:** `include/storage_assets.h`
- **Implementation:** `storage_assets.c`
- **Dependencies:** `esp_littlefs`, `esp_vfs`
- **Partition:** `assets` (LittleFS, read-only in production)

## Key Features

- **Dedicated Partition:** Separate from application code and main storage.
- **LittleFS Backend:** Efficient wear-leveling filesystem optimized for flash.
- **Read-Only Access:** Assets are flashed once and cannot be modified at runtime.
- **Auto-Discovery:** Automatically lists all files in partition on initialization.
- **Memory Management:** Helper function to load entire files with automatic allocation.
- **Directory Traversal:** Recursive directory listing for debugging.

## Typical Use Cases

- **Graphical Assets:** Logos, icons, sprites, bitmaps for displays.
- **Fonts:** Pre-compiled font files for text rendering.
- **Configuration Templates:** Default configuration files.
- **Audio Samples:** Short sound effects or melodies.
- **IR/RF Databases:** Preloaded signal databases.
- **Firmware Resources:** Any read-only data needed by the application.

## Configuration

### Partition Table

The assets partition must be defined in your partition table (`partitions.csv`):

```csv
# Name,     Type, SubType, Offset,  Size,    Flags
nvs,        data, nvs,     0x9000,  0x6000,
phy_init,   data, phy,     0xf000,  0x1000,
factory,    app,  factory, 0x10000, 1M,
assets,     data, spiffs,  0x110000, 512K,
storage,    data, spiffs,  0x190000, 1M,
```

**Important Notes:**
- The SubType must be `spiffs` (even though we use LittleFS - this is an ESP-IDF quirk).
- Size should be sufficient for all your assets (adjust as needed).
- The partition must be flashed before use.

### Constants

```c
#define ASSETS_MOUNT_POINT "/assets"
#define ASSETS_PARTITION_LABEL "assets"
```

These are defined internally and cannot be changed without modifying the source.

---

## API Reference

### Initialization

#### `storage_assets_init`

```c
esp_err_t storage_assets_init(void);
```

Initializes and mounts the assets partition. Must be called before any other asset operations.

**Behavior:**
- Mounts the LittleFS partition at `/assets`.
- Formats the partition if mounting fails (useful for first flash).
- Lists all files in the partition for debugging.
- Displays partition size and usage statistics.

**Returns:**
- `ESP_OK` - Assets partition mounted successfully.
- `ESP_ERR_NOT_FOUND` - Partition 'assets' not found in partition table.
- `ESP_FAIL` - Mount or format failed.
- `ESP_ERR_INVALID_STATE` - Already initialized.

**Example:**
```c
void app_main(void) {
    esp_err_t ret = storage_assets_init();
    if (ret == ESP_OK) {
        printf("Assets ready!\n");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        printf("ERROR: 'assets' partition not found!\n");
        printf("Check your partition table.\n");
    } else {
        printf("Assets init failed: %s\n", esp_err_to_name(ret));
    }
}
```

**Console Output Example:**
```
I (1234) storage_assets: Initializing LittleFS for assets partition
I (1245) storage_assets: Assets ready at /assets
I (1246) storage_assets: Partition size: 524288 bytes, used: 12345 bytes
I (1247) storage_assets: === Files in assets partition ===
I (1248) storage_assets:   [1] logo.bin (1200 bytes)
I (1249) storage_assets:   [DIR] fonts/
I (1250) storage_assets:     [2] arial.ttf (45000 bytes)
I (1251) storage_assets:   [3] config_template.json (567 bytes)
I (1252) storage_assets: Total: 3 file(s), 1 dir(s)
I (1253) storage_assets: ================================
```

---

#### `storage_assets_deinit`

```c
esp_err_t storage_assets_deinit(void);
```

Unmounts the assets partition and releases resources.

**Returns:**
- `ESP_OK` - Unmounted successfully.
- `ESP_ERR_INVALID_STATE` - Not initialized.

**Example:**
```c
// Before system shutdown
storage_assets_deinit();
```

---

#### `storage_assets_is_mounted`

```c
bool storage_assets_is_mounted(void);
```

Checks if the assets partition is currently mounted.

**Returns:**
- `true` - Partition is mounted and ready.
- `false` - Partition is not mounted.

**Example:**
```c
if (!storage_assets_is_mounted()) {
    storage_assets_init();
}
```

---

### File Access

#### `storage_assets_get_file_size`

```c
esp_err_t storage_assets_get_file_size(const char *filename, size_t *out_size);
```

Gets the size of a file in the assets partition without reading it.

**Parameters:**
- `filename` - Name of the file (e.g., "logo.bin", "fonts/arial.ttf").
- `out_size` - Pointer to store file size in bytes.

**Returns:**
- `ESP_OK` - Size retrieved successfully.
- `ESP_ERR_INVALID_STATE` - Assets not initialized.
- `ESP_ERR_INVALID_ARG` - NULL parameters.
- `ESP_ERR_NOT_FOUND` - File doesn't exist.

**Example:**
```c
size_t logo_size;
if (storage_assets_get_file_size("logo.bin", &logo_size) == ESP_OK) {
    printf("Logo is %zu bytes\n", logo_size);
    
    // Allocate buffer of exact size
    uint8_t *buffer = malloc(logo_size);
}
```

---

#### `storage_assets_read_file`

```c
esp_err_t storage_assets_read_file(const char *filename, uint8_t *buffer, size_t size, size_t *out_read);
```

Reads file content into a pre-allocated buffer.

**Parameters:**
- `filename` - Name of the file.
- `buffer` - Pre-allocated buffer to receive data.
- `size` - Maximum bytes to read (buffer size).
- `out_read` - Pointer to store actual bytes read (can be NULL).

**Returns:**
- `ESP_OK` - File read successfully.
- `ESP_ERR_INVALID_STATE` - Assets not initialized.
- `ESP_ERR_INVALID_ARG` - Invalid parameters.
- `ESP_ERR_NOT_FOUND` - File doesn't exist.

**Example:**
```c
uint8_t buffer[2048];
size_t bytes_read;

esp_err_t ret = storage_assets_read_file("config.json", buffer, sizeof(buffer), &bytes_read);
if (ret == ESP_OK) {
    buffer[bytes_read] = '\0';  // Null-terminate if text
    printf("Config: %s\n", (char *)buffer);
} else {
    printf("Failed to read config: %s\n", esp_err_to_name(ret));
}
```

---

#### `storage_assets_load_file`

```c
uint8_t* storage_assets_load_file(const char *filename, size_t *out_size);
```

Loads an entire file into dynamically allocated memory. **Caller must free() the returned pointer.**

**Parameters:**
- `filename` - Name of the file.
- `out_size` - Pointer to store file size (can be NULL).

**Returns:**
- Pointer to allocated buffer containing file data.
- `NULL` on error (allocation failure, file not found, etc.).

**Example:**
```c
size_t image_size;
uint8_t *image_data = storage_assets_load_file("splash_screen.bin", &image_size);

if (image_data != NULL) {
    // Use the image data
    display_draw_bitmap(image_data, image_size);
    
    // IMPORTANT: Free when done!
    free(image_data);
} else {
    printf("Failed to load splash screen\n");
}
```

**Memory Warning:** This function allocates heap memory. Ensure sufficient heap is available before loading large files.

---

### Utility Functions

#### `storage_assets_get_mount_point`

```c
const char* storage_assets_get_mount_point(void);
```

Returns the mount point path for the assets partition.

**Returns:**
- Constant string "/assets".

**Example:**
```c
const char *mount = storage_assets_get_mount_point();

// Construct full path
char full_path[128];
snprintf(full_path, sizeof(full_path), "%s/%s", mount, "config.json");

// Use with standard file operations
FILE *f = fopen(full_path, "r");
```

---

#### `storage_assets_print_info`

```c
void storage_assets_print_info(void);
```

Prints detailed information about the assets partition to the console.

**Parameters:** None

**Returns:** Nothing (void)

**Example Output:**
```
I (1234) storage_assets: === Assets Partition Info ===
I (1235) storage_assets: Mount point: /assets
I (1236) storage_assets: Partition: assets
I (1237) storage_assets: Total size: 524288 bytes (512.00 KB)
I (1238) storage_assets: Used: 98765 bytes (96.45 KB)
I (1239) storage_assets: Free: 425523 bytes (415.55 KB)
I (1240) storage_assets: Usage: 18.8%
```

**Usage:**
```c
// During debugging or diagnostics
storage_assets_print_info();
```

---

## Implementation Details

### Directory Listing

The component includes a recursive directory listing function that runs automatically during initialization:

```c
static void list_directory_recursive(const char *path, const char *prefix, 
                                     int *file_count, int *dir_count);
```

This helps during development to verify that assets were flashed correctly.

### Path Handling

All file operations internally prepend the mount point:

```c
// User provides: "logo.bin"
// Internally becomes: "/assets/logo.bin"
```

Subdirectories are supported:
```c
// User provides: "fonts/arial.ttf"
// Internally becomes: "/assets/fonts/arial.ttf"
```

### Error Handling

All functions validate:
- Initialization state
- Parameter validity
- File existence
- Memory allocation success

Always check return values to ensure robust operation.

---

## Usage Patterns

### Loading a Bitmap for Display

```c
void display_splash_screen(void) {
    size_t image_size;
    uint8_t *image = storage_assets_load_file("splash.bin", &image_size);
    
    if (image == NULL) {
        ESP_LOGE(TAG, "Failed to load splash screen");
        return;
    }
    
    // Expected format: 128x64 monochrome bitmap
    if (image_size != (128 * 64) / 8) {
        ESP_LOGW(TAG, "Unexpected image size: %zu", image_size);
    }
    
    // Send to display
    oled_draw_bitmap(0, 0, image, 128, 64);
    
    // Clean up
    free(image);
}
```

---

### Loading Configuration Template

```c
cJSON* load_default_config(void) {
    uint8_t *json_data = storage_assets_load_file("config_template.json", NULL);
    if (json_data == NULL) {
        return NULL;
    }
    
    cJSON *config = cJSON_Parse((const char *)json_data);
    free(json_data);
    
    return config;
}
```

---

### Preloading Assets at Boot

```c
typedef struct {
    uint8_t *logo_data;
    size_t logo_size;
    uint8_t *font_data;
    size_t font_size;
} app_assets_t;

app_assets_t g_assets = {0};

esp_err_t preload_assets(void) {
    // Load logo
    g_assets.logo_data = storage_assets_load_file("logo.bin", &g_assets.logo_size);
    if (g_assets.logo_data == NULL) {
        return ESP_FAIL;
    }
    
    // Load font
    g_assets.font_data = storage_assets_load_file("font.bin", &g_assets.font_size);
    if (g_assets.font_data == NULL) {
        free(g_assets.logo_data);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Assets preloaded (%zu + %zu bytes)", 
             g_assets.logo_size, g_assets.font_size);
    
    return ESP_OK;
}

void cleanup_assets(void) {
    free(g_assets.logo_data);
    free(g_assets.font_data);
    memset(&g_assets, 0, sizeof(g_assets));
}
```

---

### Chunked Reading for Large Files

```c
esp_err_t process_large_asset(const char *filename) {
    FILE *f = fopen("/assets/large_file.dat", "rb");
    if (!f) {
        return ESP_FAIL;
    }
    
    uint8_t chunk[512];
    size_t bytes_read;
    
    while ((bytes_read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        // Process chunk
        process_data(chunk, bytes_read);
    }
    
    fclose(f);
    return ESP_OK;
}
```

---

### Conditional Asset Loading

```c
void load_language_assets(const char *language) {
    char filename[64];
    snprintf(filename, sizeof(filename), "strings_%s.json", language);
    
    uint8_t *strings = storage_assets_load_file(filename, NULL);
    if (strings == NULL) {
        ESP_LOGW(TAG, "Language '%s' not found, using default", language);
        strings = storage_assets_load_file("strings_en.json", NULL);
    }
    
    if (strings != NULL) {
        parse_language_strings((const char *)strings);
        free(strings);
    }
}
```

---

## Flashing Assets

### Option 1: Automatic (Recommended)

Add to your `CMakeLists.txt`:

```cmake
# Create assets partition image from 'assets' folder
littlefs_create_partition_image(assets assets FLASH_IN_PROJECT)
```

This automatically flashes the `assets/` folder content when running `idf.py flash`.

### Option 2: Manual Flash

```bash
# Build the assets partition image
idf.py build

# Flash everything including assets
idf.py flash

# Or flash only assets partition
esptool.py write_flash 0x110000 build/assets.bin
```

**Note:** Replace `0x110000` with the actual offset from your partition table.

### Asset Folder Structure

```
project/
├── assets/
│   ├── logo.bin
│   ├── config_template.json
│   ├── fonts/
│   │   ├── arial.ttf
│   │   └── mono.ttf
│   └── images/
│       ├── icon_wifi.bin
│       └── icon_battery.bin
└── main/
    └── main.c
```

---

## Troubleshooting

### "Partition 'assets' not found"

**Problem:** The assets partition is not defined in the partition table.

**Solution:**
1. Add partition to `partitions.csv`:
   ```csv
   assets, data, spiffs, 0x110000, 512K,
   ```
2. Set partition table in `sdkconfig`:
   ```
   CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
   CONFIG_PARTITION_TABLE_CUSTOM=y
   ```
3. Rebuild: `idf.py fullclean && idf.py build`

---

### "(empty - partition has no files!)"

**Problem:** Assets partition exists but contains no files.

**Solution:**
1. Create `assets/` folder in project root
2. Add files to the folder
3. Enable automatic flash in `CMakeLists.txt`:
   ```cmake
   littlefs_create_partition_image(assets assets FLASH_IN_PROJECT)
   ```
4. Rebuild and flash: `idf.py flash`

---

### "Failed to allocate memory"

**Problem:** Insufficient heap for large asset file.

**Solutions:**
- Use `storage_assets_read_file()` with pre-allocated buffer instead of `load_file()`
- Read file in chunks instead of loading entirely
- Increase heap size in `sdkconfig`:
  ```
  CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
  CONFIG_FREERTOS_HZ=1000
  ```

---

### File Not Found at Runtime

**Problem:** File exists in assets folder but not found at runtime.

**Checklist:**
- [ ] Is partition flashed? (`idf.py flash`)
- [ ] Is filename correct? (case-sensitive!)
- [ ] Is `storage_assets_init()` called before reading?
- [ ] Check `storage_assets_print_info()` output - does it list your file?

---

## Performance Considerations

- **Initialization:** Takes 100-500ms depending on partition size and file count.
- **File Reading:** LittleFS is optimized for small files (< 1MB).
- **Memory:** `load_file()` allocates heap - monitor with `esp_get_free_heap_size()`.
- **Large Files:** For files > 100KB, consider chunked reading instead of full load.

---

## Best Practices

1. **Keep Assets Small:** LittleFS works best with many small files rather than few large ones.
2. **Compress When Possible:** Pre-compress assets (e.g., PNG → binary bitmap) before flashing.
3. **Validate Sizes:** Always check file sizes match expected values.
4. **Free Memory:** Always `free()` pointers returned by `load_file()`.
5. **Handle Errors:** Never assume assets are present - always validate return codes.
6. **Use Subdirectories:** Organize assets logically (fonts/, images/, sounds/).
7. **Version Assets:** Include version info in filenames or metadata for updates.