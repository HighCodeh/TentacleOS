# Virtual File System (VFS) - Unified Storage Abstraction

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