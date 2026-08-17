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

## Boot-loop detection (item 4)

Directly addresses the "reboots by itself" symptom: a marginal-hardware panic
(display FFC, bad SD, an I2C peripheral not ACKing) used to loop forever with no
trace and no reduced-boot attempt.

```c
void boot_report_track_bootloop(void);   // FIRST thing in app_main
bool boot_report_in_bootloop(void);
uint32_t boot_report_abnormal_boots(void);
uint32_t boot_report_panic_total(void);
void boot_report_mark_stable(void);
```

- `boot_report_track_bootloop` runs **first in `app_main`** (before
  `kernel_init`). It reads `esp_reset_reason()` and keeps a counter in
  **`RTC_NOINIT_ATTR`** memory: it survives a reset but not a power cycle and
  never writes flash - exactly the semantics for loop detection. A magic word
  distinguishes a real count from the garbage RTC RAM holds after power-on. An
  abnormal reason (panic / task-WDT / int-WDT / WDT / lockup / brownout)
  increments it; a clean reset or power-on clears it.
- It arms a one-shot `esp_timer` (20 s) that calls `boot_report_mark_stable` to
  zero the counter once the device has been up long enough to be considered
  stable, so a single crash never accumulates toward the threshold.
- At **3 consecutive abnormal boots** (`BOOT_REPORT_BOOTLOOP_THRESHOLD`),
  `boot_report_in_bootloop()` is true and `kernel_init` drops into
  [safe mode](../recovery/README.md) - the degraded target (no radios, no SD
  theme, minimal UI). The safe-mode footer shows the reset reason, and the crash
  viewer shows `Panics total` and `Abnormal boots N/3`.
- Only a **summary** is persisted to NVS (namespace `boot_report`: last reason +
  running panic total), written on abnormal boots only, so flash wear is
  negligible. The RTC counter carries the loop state itself.

## Follow-ups

- Exposing the map and crash summary over `host_link` for the companion app is
  not wired yet; the on-device viewers are the current surface.
