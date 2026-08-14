# Boot Report: Boot Map & Crash Forensics

On-device diagnostics for the ESP32-P4 covering two things: **why the last run
ended** (crash forensics, item 22) and **how this boot came up** (the boot map,
item 8). Lives in `components/Service/boot_report` so both the kernel (Core, via
the transitive Applications -> Service require) and the UI (Applications ->
Service) can reach it.

- **Header:** `components/Service/boot_report/include/boot_report.h`

## Boot map (item 8)

`kernel_init` used to discard every init return code, so a failed subsystem was
invisible. It now returns `esp_err_t` and records each subsystem into a map:

```c
typedef struct {
  const char *name;   // short label
  bool required;      // a required stage failing aborts to safe mode
  esp_err_t result;   // ESP_OK, an error, or ESP_ERR_NOT_FOUND when skipped
} boot_stage_t;

void boot_report_reset(void);                    // at the top of kernel_init
void boot_report_record(const char *name, bool required, esp_err_t result);
const boot_stage_t *boot_report_stages(int *out_count);
bool boot_report_all_required_ok(void);
```

Recorded stages with their real return codes: `sd-storage` (optional, absent SD
is fine), `assets` (required), `battery` (optional), `display` (required),
`nvs` (required). A **required** failure makes `kernel_init` drop into
[safe mode](../recovery/README.md) instead of booting blind, and the function
returns `ESP_FAIL`.

Viewer: **View boot map** in the developer menu (`SCREEN_BOOT_MAP`) and in the
safe-mode menu. Each row shows the stage name (`*` marks required) and its state
(`OK` / an `ESP_ERR_*` name / `skip`).

## Crash forensics (item 22)

### Flash layout

Core dump is enabled to a dedicated flash partition (previously
`ESP_COREDUMP_ENABLE_TO_NONE`, so a field crash left nothing behind).

- `partitions.csv`: a 64K `coredump` partition, carved from the OTA slots
  (`ota_0`/`ota_1` shrunk `0x280000` -> `0x270000`, ~60% used so plenty of
  headroom). The `assets` partition is 100% full and is **not** touched; it just
  moves from `0x520000` to `0x510000`.
- `sdkconfig.defaults`: `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`,
  `..._DATA_FORMAT_ELF`, `..._CHECKSUM_CRC32`.

> Because the partition table changed, the next update must be a **full flash**
> (`idf.py flash`: bootloader + partition table + app + assets). OTA from the old
> layout will not work, and the coredump partition starts blank (so "no crash
> recorded" is expected until a real panic).

### Capture API

```c
typedef struct {
  esp_reset_reason_t reason;
  bool has_coredump;
  bool crash;              // reason or dump indicates an abnormal end
  char task[16];           // faulting task (coredump only)
  uint32_t pc, mcause, mtval, ra, sp; // RISC-V fault context (coredump only)
} crash_info_t;

void boot_report_capture_crash(void);  // early in kernel_init
bool boot_report_has_crash(void);
const crash_info_t *boot_report_crash(void);
esp_err_t boot_report_clear_crash(void);
const char *boot_report_reason_str(esp_reset_reason_t reason);
```

`boot_report_capture_crash` runs early in `kernel_init` (right after NVS, before
anything can overwrite the reason). It reads `esp_reset_reason()` and, if
`esp_core_dump_image_check()` passes, `esp_core_dump_get_summary()` for the
faulting task and RISC-V registers. RISC-V has no on-device backtrace
symbolication (that needs GDB/the ELF on a host), so the viewer shows the raw
`pc`/`ra`/`sp`/`mcause`/`mtval` for offline decoding.

Viewer: **View last crash** in the developer menu (`SCREEN_CRASH_REPORT`) and in
the safe-mode menu. Shows the reset reason and, when a dump exists, the fault
context; **OK** clears the dump (`esp_core_dump_image_erase`).

## Follow-ups

- Exposing the map and crash summary over `host_link` for the companion app is
  not wired yet; the on-device viewers are the current surface.
