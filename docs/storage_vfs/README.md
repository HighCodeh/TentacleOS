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

# P4

The VFS system provides a unified, low-level abstraction layer for multiple storage backends, allowing applications to work with files using a consistent API regardless of the underlying storage medium (SD Card, SPIFFS, LittleFS, or RAM).

## Overview

- **Location:** `components/Service/storage_vfs/`
- **Main Headers:** 
  - `include/vfs_core.h` (Core API)
  - `include/vfs_config.h` (Backend selection)
  - `include/vfs_sdcard.h` (SD Card backend)
  - `include/vfs_littlefs.h` (LittleFS backend)
- **Dependencies:** `esp_vfs`, `esp_vfs_fat`, `esp_littlefs`, `sdmmc`, `spi`

## Architecture Position

```
Application Code
      ↓
  Storage API    ← Recommended for most applications
      ↓
   VFS Core      ← You are here (low-level abstraction)
      ↓
Backend-Specific Drivers (SD/LittleFS/SPIFFS/RAM)
```

**When to use VFS directly:**
- You need POSIX-like file descriptor operations
- You want manual control over open/read/write/close
- Storage API doesn't provide what you need
- You're building your own storage abstraction

**When NOT to use VFS:**
- For simple file operations → Use **Storage API** instead
- For read-only assets → Use **Storage Assets** instead

---

## Key Features

- **Multiple Backends:** Support for SD Card (FAT), SPIFFS, LittleFS, and RAM filesystem
- **Single Backend Selection:** Compile-time selection ensures only one backend is active
- **POSIX-Like API:** Familiar file operations (open, read, write, close, lseek)
- **Directory Operations:** Full directory tree manipulation
- **Backend Abstraction:** Switch storage backends by changing configuration

---

## Backend Selection (Compile-Time)

The VFS system uses **compile-time backend selection** to ensure only one storage backend is active.

Edit `vfs_config.h`:

```c
// Only ONE backend can be uncommented at a time

#define VFS_USE_SD_CARD     // ← Active backend
// #define VFS_USE_SPIFFS
// #define VFS_USE_LITTLEFS
// #define VFS_USE_RAMFS
```

**Important:** The system validates this at compile time and will error if multiple backends are selected.

### Backend Configurations

Each backend has specific configuration in `vfs_config.h`:

#### SD Card Backend
```c
#define VFS_MOUNT_POINT     "/sdcard"
#define VFS_MAX_FILES       10
#define VFS_FORMAT_ON_FAIL  false
#define VFS_BACKEND_NAME    "SD Card"
```

#### LittleFS Backend
```c
#define VFS_MOUNT_POINT     "/littlefs"
#define VFS_MAX_FILES       10
#define VFS_FORMAT_ON_FAIL  true
#define VFS_PARTITION_LABEL "storage"
#define VFS_BACKEND_NAME    "LittleFS"
```

---

## Data Structures

### File Descriptor

```c
typedef int vfs_fd_t;
#define VFS_INVALID_FD  -1
```

File descriptor for open files. Similar to POSIX file descriptors.

---

### File/Directory Information

```c
typedef struct {
    char name[VFS_MAX_NAME];      // Entry name (64 chars max)
    vfs_entry_type_t type;        // VFS_TYPE_FILE or VFS_TYPE_DIR
    size_t size;                  // File size in bytes
    time_t mtime;                 // Last modification time
    time_t ctime;                 // Creation time
    bool is_hidden;               // Hidden attribute
    bool is_readonly;             // Read-only attribute
} vfs_stat_t;
```

---

### Filesystem Statistics

```c
typedef struct {
    uint64_t total_bytes;    // Total filesystem capacity
    uint64_t free_bytes;     // Available free space
    uint64_t used_bytes;     // Space currently in use
    uint32_t block_size;     // Filesystem block size
    uint32_t total_blocks;   // Total number of blocks
    uint32_t free_blocks;    // Available free blocks
} vfs_statvfs_t;
```

---

## Core API Reference

### Initialization

#### `vfs_init_auto`

```c
esp_err_t vfs_init_auto(void);
```

Initializes the VFS backend selected in `vfs_config.h`.

**Returns:**
- `ESP_OK` - Backend initialized and mounted successfully
- `ESP_FAIL` - Initialization failed (check logs)

---

#### `vfs_deinit_auto`

```c
esp_err_t vfs_deinit_auto(void);
```

Unmounts and deinitializes the active VFS backend.

**Returns:**
- `ESP_OK` - Backend deinitialized successfully
- `ESP_FAIL` - Deinitialization failed

---

#### `vfs_is_mounted_auto`

```c
bool vfs_is_mounted_auto(void);
```

Checks if the active backend is currently mounted.

---

#### `vfs_get_mount_point`

```c
const char* vfs_get_mount_point(void);
```

Returns the mount point path for the active backend (e.g., "/sdcard", "/littlefs").

---

#### `vfs_get_backend_name`

```c
const char* vfs_get_backend_name(void);
```

Returns the human-readable name of the active backend (e.g., "SD Card", "LittleFS").

---

#### `vfs_print_info`

```c
void vfs_print_info(void);
```

Prints detailed information about the active VFS backend to the console, including mount point, capacity, and usage statistics.

---

### File Operations (POSIX-like)

#### `vfs_open`

```c
vfs_fd_t vfs_open(const char *path, int flags, int mode);
```

Opens a file with specified flags and permissions.

**Parameters:**
- `path` - Full path to file (e.g., "/sdcard/data.txt")
- `flags` - Opening mode flags (bitwise OR):
  - `VFS_O_RDONLY` - Read-only
  - `VFS_O_WRONLY` - Write-only
  - `VFS_O_RDWR` - Read and write
  - `VFS_O_CREAT` - Create if doesn't exist
  - `VFS_O_TRUNC` - Truncate to zero length
  - `VFS_O_APPEND` - Append to end of file
  - `VFS_O_EXCL` - Fail if file exists (with O_CREAT)
- `mode` - File permissions (POSIX mode, e.g., 0644)

**Returns:**
- Valid file descriptor (>= 0) on success
- `VFS_INVALID_FD` on failure

---

#### `vfs_read`

```c
ssize_t vfs_read(vfs_fd_t fd, void *buf, size_t size);
```

Reads data from an open file.

**Returns:**
- Number of bytes read (>= 0)
- -1 on error

---

#### `vfs_write`

```c
ssize_t vfs_write(vfs_fd_t fd, const void *buf, size_t size);
```

Writes data to an open file.

**Returns:**
- Number of bytes written (>= 0)
- -1 on error

---

#### `vfs_lseek`

```c
off_t vfs_lseek(vfs_fd_t fd, off_t offset, int whence);
```

Moves the file position pointer.

**Parameters:**
- `whence` - Reference point:
  - `VFS_SEEK_SET` - From beginning of file
  - `VFS_SEEK_CUR` - From current position
  - `VFS_SEEK_END` - From end of file

**Returns:**
- New file position on success
- -1 on error

---

#### `vfs_close`

```c
esp_err_t vfs_close(vfs_fd_t fd);
```

Closes an open file descriptor.

---

#### `vfs_fsync`

```c
esp_err_t vfs_fsync(vfs_fd_t fd);
```

Flushes file buffers to storage, ensuring data is physically written.

---

### File Metadata

#### `vfs_stat`

```c
esp_err_t vfs_stat(const char *path, vfs_stat_t *st);
```

Gets information about a file or directory.

---

#### `vfs_exists`

```c
bool vfs_exists(const char *path);
```

Checks if a file or directory exists.

---

#### `vfs_get_size`

```c
esp_err_t vfs_get_size(const char *path, size_t *size);
```

Gets the size of a file in bytes.

---

### File Management

#### `vfs_rename`

```c
esp_err_t vfs_rename(const char *old_path, const char *new_path);
```

Renames or moves a file.

---

#### `vfs_unlink`

```c
esp_err_t vfs_unlink(const char *path);
```

Deletes a file.

---

#### `vfs_truncate`

```c
esp_err_t vfs_truncate(const char *path, off_t length);
```

Resizes a file to the specified length.

---

### Directory Operations

#### `vfs_mkdir`

```c
esp_err_t vfs_mkdir(const char *path, int mode);
```

Creates a new directory.

---

#### `vfs_rmdir`

```c
esp_err_t vfs_rmdir(const char *path);
```

Removes an empty directory.

---

#### `vfs_rmdir_recursive`

```c
esp_err_t vfs_rmdir_recursive(const char *path);
```

Recursively removes a directory and all its contents.

---

#### `vfs_opendir` / `vfs_readdir` / `vfs_closedir`

```c
vfs_dir_t vfs_opendir(const char *path);
esp_err_t vfs_readdir(vfs_dir_t dir, vfs_stat_t *entry);
esp_err_t vfs_closedir(vfs_dir_t dir);
```

Directory traversal using iterator pattern.

---

#### `vfs_list_dir`

```c
typedef void (*vfs_dir_callback_t)(const vfs_stat_t *entry, void *user_data);
esp_err_t vfs_list_dir(const char *path, vfs_dir_callback_t callback, void *user_data);
```

Lists directory contents using callback.

---

### Filesystem Information

#### `vfs_statvfs`

```c
esp_err_t vfs_statvfs(const char *path, vfs_statvfs_t *stat);
```

Gets filesystem statistics.

---

#### `vfs_get_free_space`

```c
esp_err_t vfs_get_free_space(const char *path, uint64_t *free_bytes);
```

Gets available free space.

---

#### `vfs_get_usage_percent`

```c
esp_err_t vfs_get_usage_percent(const char *path, float *percentage);
```

Calculates filesystem usage percentage.

---

### High-Level Helpers

These functions simplify common operations by handling open/close internally.

#### `vfs_read_file`

```c
esp_err_t vfs_read_file(const char *path, void *buf, size_t size, size_t *bytes_read);
```

Reads entire file content in one operation.

---

#### `vfs_write_file`

```c
esp_err_t vfs_write_file(const char *path, const void *buf, size_t size);
```

Writes data to file, creating or overwriting it.

---

#### `vfs_append_file`

```c
esp_err_t vfs_append_file(const char *path, const void *buf, size_t size);
```

Appends data to end of file.

---

#### `vfs_copy_file`

```c
esp_err_t vfs_copy_file(const char *src, const char *dst);
```

Copies a file.

---

## Backend-Specific APIs

### SD Card Backend

```c
#include "vfs_sdcard.h"

esp_err_t vfs_sdcard_init(void);
esp_err_t vfs_sdcard_deinit(void);
bool vfs_sdcard_is_mounted(void);
void vfs_sdcard_print_info(void);
bool vfs_sdcard_get_name(char *out, size_t n);
esp_err_t vfs_sdcard_format(void);
esp_err_t vfs_register_sd_backend(void);
esp_err_t vfs_unregister_sd_backend(void);

// Raw handoff for USB Mass Storage (see "USB Mass Storage (MSC)" below)
esp_err_t vfs_sdcard_detach_for_msc(void **out_card);
esp_err_t vfs_sdcard_reattach_after_msc(void *card);
```

### LittleFS Backend

```c
#include "vfs_littlefs.h"

esp_err_t vfs_littlefs_init(void);
esp_err_t vfs_littlefs_deinit(void);
bool vfs_littlefs_is_mounted(void);
void vfs_littlefs_print_info(void);
esp_err_t vfs_littlefs_format(void);
```

---

## USB Mass Storage (MSC)

> P4 only. The MSC path lives in `storage_vfs`
> (`usb_msc.c` / `vfs_sdcard.c`, headers `include/usb_msc.h` and
> `include/vfs_sdcard.h`). The C5 has no USB connector or SD card and does not
> build these.

USB Storage exposes the entire microSD as raw sectors, including its partition
map. SDMMC remains initialized at **40 MHz, 4-bit** throughout entry and exit.
The application and USB host never deliberately mount the filesystem together.

### Ownership and recovery

`usb_msc_enter()` atomically claims the transition, then calls
`esp_vfs_fat_suspend()` under the FatFS VFS context lock. Existing operations
finish before suspension; open files or directory streams reject entry without
closing them or restarting the device. The retained VFS context remains valid,
but its FatFS volume is unmounted, so subsequent stdio and VFS opens cannot
access the card. Filesystem statistics use the same lock. The SD host/card and
diskio registration remain allocated; no clock/slot reinitialization occurs.

`usb_msc_exit()` hides MSC and queues a barrier on the TinyUSB task. Every
synchronous sector operation finishes before the filesystem is resumed. A
remount failure triggers one CMD12 STOP_TRANSMISSION (one-second command timeout),
then at most one additional mount attempt if CMD12 succeeds. A persistent failure
retains the card, reports `USB_MSC_SD_UNAVAILABLE`, and allows BACK to return home
without issuing more SD commands. The filesystem stays suspended, MSC stays
hidden, and new USB Storage entries are refused until restart. There is no
automatic reboot or formatting. Calls must run on workers, never the LVGL thread. Atomic
transitions reject concurrent entry/exit requests.

The UI's Connected status means USB enumeration, not a mounted host volume.
BACK while connected asks for confirmation that the volume was ejected; OK
exits and BACK cancels. A host filesystem unmount need not send SCSI eject.
Worker creation failures are displayed instead of leaving a permanent spinner.

### Sector transport

The first-party `usb_msc.c` implements TinyUSB MSC callbacks directly. It does
not instantiate esp_tinyusb's storage wrapper: version 2.4.0 still acknowledges
deferred writes before their SD result is known. Read/write callbacks now
complete SDMMC I/O before returning success and propagate failures as SCSI
medium errors. The first read/write failure latches the LUN unavailable until
exit; subsequent host requests do not flood the failed card with more I/O.
Writes use one SD sector per command because IDF 6.1 multi-block error recovery
can overwrite the original error with the result of CMD22. This favors correct
error reporting over multi-block write throughput; the bus remains at 40 MHz.
Address arithmetic is 64-bit, checks capacity and sector
alignment, and includes the callback offset. The 1024-byte TinyUSB endpoint
buffer supports whole 512-byte SD sectors; partial-sector requests are rejected.
SYNCHRONIZE CACHE checks card status after preceding synchronous writes. Eject
marks the LUN unavailable without mounting it inside the USB callback.

The `msc_storage_mount_to_app/usb` integration hooks intentionally do nothing:
esp_tinyusb's bus notifications cannot transfer filesystem ownership. Their
signatures must be reviewed when upgrading esp_tinyusb. No managed source is
modified. The existing TinyUSB stack handles USB BOT and endpoint transfers.

### Native diagnostics

During MSC, CDC interface 0 outputs plain-text P4 logs through the existing
bounded host-link log queue. It does not accept companion binary commands in
this mode. Open the new `/dev/cu.usbmodem*` port on the Mac to observe SD errors
while UART is hidden by the mux. Output is best effort and may drop lines when
the host cannot keep up; a disconnected terminal cannot block storage I/O.
Leaving MSC restores the CDC companion protocol on subsequent native use.

### Validation

`node --test tools/highboy/tests/test_usb_msc.mjs` compiles the production MSC
implementation with mocked hardware. It covers repeated cycles, busy entry,
remount retry, transition reentrancy, USB-task drain, invalid/out-of-range I/O,
read/write errors, flush and eject. `python tools/fatfs/test_fatfs.py --idf
"$IDF_PATH"` validates the verified override and RAM-disk FAT/exFAT round trips.
`python tools/highboy/tests/test_usb_descriptors.py` checks actual descriptor
arrays with upstream TinyUSB macros under ASan/UBSan. `python
tools/highboy/tests/test_usb_storage_input.py` covers exit confirmation and
worker-allocation failure and leaving the screen with an unavailable SD.
`python tools/highboy/tests/test_usb_sd_recovery.py` tests the bounded recovery
and both command/transport error paths. These do not replace physical USB/SD
validation. The timeout observed on the physical card is not proven resolved
by these host tests.

After flashing P4, use a backed-up test card: enter Storage, verify capacity,
copy a file in both directions and compare hashes, eject in macOS, confirm exit
on Highboy, and repeat several times. Capture native CDC logs if the volume
fails to mount. Neither First Aid repair nor formatting is required by this
implementation. The previous macOS failures are not proven fixed until this
hardware test passes.

---

## Switching Backends

To switch between storage backends, edit `vfs_config.h`:

```c
// From SD Card:
#define VFS_USE_SD_CARD

// To LittleFS:
// #define VFS_USE_SD_CARD
#define VFS_USE_LITTLEFS
```

Rebuild your project. All `vfs_*` function calls remain the same.

---

## Best Practices

1. **Consider Storage API first** - Use VFS only when you need low-level control
2. **Always check return values** - Especially for `vfs_open()` and `vfs_init_auto()`
3. **Close file descriptors** - Always call `vfs_close()` when done
4. **Use absolute paths** - Include mount point (e.g., "/sdcard/file.txt")
5. **Single backend only** - Never uncomment multiple backends in `vfs_config.h`
---

# C5

The VFS system provides a unified, low-level abstraction layer for multiple storage backends, allowing applications to work with files using a consistent API regardless of the underlying storage medium (SD Card, SPIFFS, LittleFS, or RAM).

## Overview

- **Location:** `components/storage/vfs/`
- **Main Headers:** 
  - `include/vfs_core.h` (Core API)
  - `include/vfs_config.h` (Backend selection)
  - `include/vfs_sdcard.h` (SD Card backend)
  - `include/vfs_littlefs.h` (LittleFS backend)
- **Dependencies:** `esp_vfs`, `esp_vfs_fat`, `esp_littlefs`, `sdmmc`, `spi`

## Architecture Position

```
Application Code
      ↓
  Storage API    ← Recommended for most applications
      ↓
   VFS Core      ← You are here (low-level abstraction)
      ↓
Backend-Specific Drivers (SD/LittleFS/SPIFFS/RAM)
```

**When to use VFS directly:**
- You need POSIX-like file descriptor operations
- You want manual control over open/read/write/close
- Storage API doesn't provide what you need
- You're building your own storage abstraction

**When NOT to use VFS:**
- For simple file operations → Use **Storage API** instead
- For read-only assets → Use **Storage Assets** instead

---

## Key Features

- **Multiple Backends:** Support for SD Card (FAT), SPIFFS, LittleFS, and RAM filesystem
- **Single Backend Selection:** Compile-time selection ensures only one backend is active
- **POSIX-Like API:** Familiar file operations (open, read, write, close, lseek)
- **Directory Operations:** Full directory tree manipulation
- **Backend Abstraction:** Switch storage backends by changing configuration

---

## Backend Selection (Compile-Time)

The VFS system uses **compile-time backend selection** to ensure only one storage backend is active.

Edit `vfs_config.h`:

```c
// Only ONE backend can be uncommented at a time

#define VFS_USE_SD_CARD     // ← Active backend
// #define VFS_USE_SPIFFS
// #define VFS_USE_LITTLEFS
// #define VFS_USE_RAMFS
```

**Important:** The system validates this at compile time and will error if multiple backends are selected.

### Backend Configurations

Each backend has specific configuration in `vfs_config.h`:

#### SD Card Backend
```c
#define VFS_MOUNT_POINT     "/sdcard"
#define VFS_MAX_FILES       10
#define VFS_FORMAT_ON_FAIL  false
#define VFS_BACKEND_NAME    "SD Card"
```

#### LittleFS Backend
```c
#define VFS_MOUNT_POINT     "/littlefs"
#define VFS_MAX_FILES       10
#define VFS_FORMAT_ON_FAIL  true
#define VFS_PARTITION_LABEL "storage"
#define VFS_BACKEND_NAME    "LittleFS"
```

---

## Data Structures

### File Descriptor

```c
typedef int vfs_fd_t;
#define VFS_INVALID_FD  -1
```

File descriptor for open files. Similar to POSIX file descriptors.

---

### File/Directory Information

```c
typedef struct {
    char name[VFS_MAX_NAME];      // Entry name (64 chars max)
    vfs_entry_type_t type;        // VFS_TYPE_FILE or VFS_TYPE_DIR
    size_t size;                  // File size in bytes
    time_t mtime;                 // Last modification time
    time_t ctime;                 // Creation time
    bool is_hidden;               // Hidden attribute
    bool is_readonly;             // Read-only attribute
} vfs_stat_t;
```

---

### Filesystem Statistics

```c
typedef struct {
    uint64_t total_bytes;    // Total filesystem capacity
    uint64_t free_bytes;     // Available free space
    uint64_t used_bytes;     // Space currently in use
    uint32_t block_size;     // Filesystem block size
    uint32_t total_blocks;   // Total number of blocks
    uint32_t free_blocks;    // Available free blocks
} vfs_statvfs_t;
```

---

## Core API Reference

### Initialization

#### `vfs_init_auto`

```c
esp_err_t vfs_init_auto(void);
```

Initializes the VFS backend selected in `vfs_config.h`.

**Returns:**
- `ESP_OK` - Backend initialized and mounted successfully
- `ESP_FAIL` - Initialization failed (check logs)

---

#### `vfs_deinit_auto`

```c
esp_err_t vfs_deinit_auto(void);
```

Unmounts and deinitializes the active VFS backend.

**Returns:**
- `ESP_OK` - Backend deinitialized successfully
- `ESP_FAIL` - Deinitialization failed

---

#### `vfs_is_mounted_auto`

```c
bool vfs_is_mounted_auto(void);
```

Checks if the active backend is currently mounted.

---

#### `vfs_get_mount_point`

```c
const char* vfs_get_mount_point(void);
```

Returns the mount point path for the active backend (e.g., "/sdcard", "/littlefs").

---

#### `vfs_get_backend_name`

```c
const char* vfs_get_backend_name(void);
```

Returns the human-readable name of the active backend (e.g., "SD Card", "LittleFS").

---

#### `vfs_print_info`

```c
void vfs_print_info(void);
```

Prints detailed information about the active VFS backend to the console, including mount point, capacity, and usage statistics.

---

### File Operations (POSIX-like)

#### `vfs_open`

```c
vfs_fd_t vfs_open(const char *path, int flags, int mode);
```

Opens a file with specified flags and permissions.

**Parameters:**
- `path` - Full path to file (e.g., "/sdcard/data.txt")
- `flags` - Opening mode flags (bitwise OR):
  - `VFS_O_RDONLY` - Read-only
  - `VFS_O_WRONLY` - Write-only
  - `VFS_O_RDWR` - Read and write
  - `VFS_O_CREAT` - Create if doesn't exist
  - `VFS_O_TRUNC` - Truncate to zero length
  - `VFS_O_APPEND` - Append to end of file
  - `VFS_O_EXCL` - Fail if file exists (with O_CREAT)
- `mode` - File permissions (POSIX mode, e.g., 0644)

**Returns:**
- Valid file descriptor (>= 0) on success
- `VFS_INVALID_FD` on failure

---

#### `vfs_read`

```c
ssize_t vfs_read(vfs_fd_t fd, void *buf, size_t size);
```

Reads data from an open file.

**Returns:**
- Number of bytes read (>= 0)
- -1 on error

---

#### `vfs_write`

```c
ssize_t vfs_write(vfs_fd_t fd, const void *buf, size_t size);
```

Writes data to an open file.

**Returns:**
- Number of bytes written (>= 0)
- -1 on error

---

#### `vfs_lseek`

```c
off_t vfs_lseek(vfs_fd_t fd, off_t offset, int whence);
```

Moves the file position pointer.

**Parameters:**
- `whence` - Reference point:
  - `VFS_SEEK_SET` - From beginning of file
  - `VFS_SEEK_CUR` - From current position
  - `VFS_SEEK_END` - From end of file

**Returns:**
- New file position on success
- -1 on error

---

#### `vfs_close`

```c
esp_err_t vfs_close(vfs_fd_t fd);
```

Closes an open file descriptor.

---

#### `vfs_fsync`

```c
esp_err_t vfs_fsync(vfs_fd_t fd);
```

Flushes file buffers to storage, ensuring data is physically written.

---

### File Metadata

#### `vfs_stat`

```c
esp_err_t vfs_stat(const char *path, vfs_stat_t *st);
```

Gets information about a file or directory.

---

#### `vfs_exists`

```c
bool vfs_exists(const char *path);
```

Checks if a file or directory exists.

---

#### `vfs_get_size`

```c
esp_err_t vfs_get_size(const char *path, size_t *size);
```

Gets the size of a file in bytes.

---

### File Management

#### `vfs_rename`

```c
esp_err_t vfs_rename(const char *old_path, const char *new_path);
```

Renames or moves a file.

---

#### `vfs_unlink`

```c
esp_err_t vfs_unlink(const char *path);
```

Deletes a file.

---

#### `vfs_truncate`

```c
esp_err_t vfs_truncate(const char *path, off_t length);
```

Resizes a file to the specified length.

---

### Directory Operations

#### `vfs_mkdir`

```c
esp_err_t vfs_mkdir(const char *path, int mode);
```

Creates a new directory.

---

#### `vfs_rmdir`

```c
esp_err_t vfs_rmdir(const char *path);
```

Removes an empty directory.

---

#### `vfs_rmdir_recursive`

```c
esp_err_t vfs_rmdir_recursive(const char *path);
```

Recursively removes a directory and all its contents.

---

#### `vfs_opendir` / `vfs_readdir` / `vfs_closedir`

```c
vfs_dir_t vfs_opendir(const char *path);
esp_err_t vfs_readdir(vfs_dir_t dir, vfs_stat_t *entry);
esp_err_t vfs_closedir(vfs_dir_t dir);
```

Directory traversal using iterator pattern.

---

#### `vfs_list_dir`

```c
typedef void (*vfs_dir_callback_t)(const vfs_stat_t *entry, void *user_data);
esp_err_t vfs_list_dir(const char *path, vfs_dir_callback_t callback, void *user_data);
```

Lists directory contents using callback.

---

### Filesystem Information

#### `vfs_statvfs`

```c
esp_err_t vfs_statvfs(const char *path, vfs_statvfs_t *stat);
```

Gets filesystem statistics.

---

#### `vfs_get_free_space`

```c
esp_err_t vfs_get_free_space(const char *path, uint64_t *free_bytes);
```

Gets available free space.

---

#### `vfs_get_usage_percent`

```c
esp_err_t vfs_get_usage_percent(const char *path, float *percentage);
```

Calculates filesystem usage percentage.

---

### High-Level Helpers

These functions simplify common operations by handling open/close internally.

#### `vfs_read_file`

```c
esp_err_t vfs_read_file(const char *path, void *buf, size_t size, size_t *bytes_read);
```

Reads entire file content in one operation.

---

#### `vfs_write_file`

```c
esp_err_t vfs_write_file(const char *path, const void *buf, size_t size);
```

Writes data to file, creating or overwriting it.

---

#### `vfs_append_file`

```c
esp_err_t vfs_append_file(const char *path, const void *buf, size_t size);
```

Appends data to end of file.

---

#### `vfs_copy_file`

```c
esp_err_t vfs_copy_file(const char *src, const char *dst);
```

Copies a file.

---

## Backend-Specific APIs

### SD Card Backend

```c
#include "vfs_sdcard.h"

esp_err_t vfs_sdcard_init(void);
esp_err_t vfs_sdcard_deinit(void);
bool vfs_sdcard_is_mounted(void);
void vfs_sdcard_print_info(void);
esp_err_t vfs_sdcard_format(void);
```

### LittleFS Backend

```c
#include "vfs_littlefs.h"

esp_err_t vfs_littlefs_init(void);
esp_err_t vfs_littlefs_deinit(void);
bool vfs_littlefs_is_mounted(void);
void vfs_littlefs_print_info(void);
esp_err_t vfs_littlefs_format(void);
```

---

## Switching Backends

To switch between storage backends, edit `vfs_config.h`:

```c
// From SD Card:
#define VFS_USE_SD_CARD

// To LittleFS:
// #define VFS_USE_SD_CARD
#define VFS_USE_LITTLEFS
```

Rebuild your project. All `vfs_*` function calls remain the same.

---

## Best Practices

1. **Consider Storage API first** - Use VFS only when you need low-level control
2. **Always check return values** - Especially for `vfs_open()` and `vfs_init_auto()`
3. **Close file descriptors** - Always call `vfs_close()` when done
4. **Use absolute paths** - Include mount point (e.g., "/sdcard/file.txt")
5. **Single backend only** - Never uncomment multiple backends in `vfs_config.h`

### SD formatting and lifecycle exclusion

Formatting, initialization, teardown and USB handoff share a non-blocking atomic
access guard. Concurrent lifecycle calls return `ESP_ERR_INVALID_STATE` instead
of releasing a card or FatFS context still in use. Health probes hold the same
guard across their complete write/read/remove sequence. A busy probe is skipped,
not reported as a media failure requiring remount.

Formatting first suspends FatFS, rejecting open files/directories and preventing
new opens while the volume is being rewritten. Allocation failure restores the
original mount; a failed remount releases the retained context exactly once.
This does not change the SDMMC clock or bus width. Regression coverage:
`python3 tools/highboy/tests/test_sd_format_lifecycle.py`.

Successful SD formatting sets the filesystem volume label to `Highboy` (FAT
normalizes it to `HIGHBOY`). This is the name shown by Finder, independently of
USB manufacturer/product descriptors. Existing volumes are not renamed at boot
or on USB entry; they can be renamed in Finder without reformatting. Label write
failures are logged and returned as errors. `CONFIG_FATFS_USE_LABEL=y` enables
the FatFS label API.

`vfs_sdcard_format_with_progress()` accepts a worker-context callback for stage
and weighted completion updates. Returning false requests cancellation between
metadata writes, reported as `ESP_ERR_NOT_FINISHED`; no task is forcibly deleted.
The guard remains held through remount/cleanup, including cancellation. Existing
format APIs use the same path without a callback. The UI publishes progress and
cancel requests atomically and never accesses LVGL from this callback.
