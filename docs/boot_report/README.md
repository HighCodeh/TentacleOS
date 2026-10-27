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

Recorded stages with their real return codes, in the order `kernel_init` runs
them: `nvs` (required), `i2c` (optional, degrades charger/haptic/LED only),
`sd-storage` (optional, absent SD is fine), `sd-health` (optional, recorded only
when the SD is mounted), `assets` (required), `battery` (optional), `audio`
(optional), `display` (required), `lvgl` (required, recorded only when the panel
came up). A **required** failure makes `kernel_init` drop into
[safe mode](../recovery/README.md) instead of booting blind, and the function
returns `ESP_FAIL`. Manual/boot-loop recovery also returns `ESP_FAIL`, so
`app_main` never confirms a pending OTA or synchronizes asset version metadata
from a recovery boot. Monitor creation failure is fatal; console creation failure
is recorded as optional. NVS initialization errors preserve the existing partition.

Viewer: **View boot map** in the developer menu (`SCREEN_BOOT_MAP`) and in the
safe-mode menu. Each row shows the stage name (`*` marks required) and its state
(`OK` / an `ESP_ERR_*` name / `skip`).

## Crash forensics (item 22)

### Flash layout

Core dump is enabled to the existing 64 KiB `coredump` partition. In this
integrated profile P4 `ota_0` is at `0x20000`, `ota_1` at `0x2F0000`, both
`0x2D0000` bytes; coredump is at `0x5C0000` and assets at `0x5D0000`.
`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`, ELF format and CRC32 are selected.
No partition, bootloader or asset migration is part of this correction.

Use the [integrated deployment contract](../highboy/README.md#flash-and-storage-boundary).
Its guarded app-only update preserves installed layout/boot/assets; full
restoration/migration remains gated. The older generic full-flash guidance is
not applicable to this checkout.

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
uint32_t boot_report_boot_count(void);
void boot_report_complete(void);       // kernel finished initialization
void boot_report_mark_healthy(void);   // healthy monitor cycle
void boot_report_mark_stable(void);    // explicit counter clear
```

- `boot_report_track_bootloop` runs **first in `app_main`** (before
  `kernel_init`). It reads `esp_reset_reason()` and keeps a counter in
  **`RTC_NOINIT_ATTR`** memory: it survives a reset but not a power cycle and
  never writes flash - exactly the semantics for loop detection. A magic word
  distinguishes a real count from the garbage RTC RAM holds after power-on. An
  abnormal reason (panic / task-WDT / int-WDT / WDT / lockup / brownout)
  increments it with saturation at `UINT32_MAX`; a clean reset or power-on clears it.
- A one-shot `esp_timer` (20 s) permits a later healthy monitor cycle to clear
  the counter. Time alone never clears it. `boot_report_complete()` must first
  publish completed kernel initialization, every required stage must have
  succeeded, and the monitor must observe task-stack, heap and render progress.
  Failed task snapshots, a stuck first render or failed required initialization
  cannot be certified stable. The current boot-loop decision remains latched
  until reset even after a healthy recovery clears the counter for the next boot.
  Timer create/start/delete failures are logged; a failed arm never clears it.
- At **3 consecutive abnormal boots** (`BOOT_REPORT_BOOTLOOP_THRESHOLD`),
  `boot_report_in_bootloop()` is true and `kernel_init` drops into
  [safe mode](../recovery/README.md) - the degraded target (no radios, no SD
  theme, minimal UI). The safe-mode footer shows the reset reason, and the crash
  viewer shows `Panics total` and `Abnormal boots N/3`.
- A **summary** is persisted to NVS (namespace `boot_report`). A real
  **total boot count** (`boot_total`, exposed via `boot_report_boot_count()`) is
  bumped once every boot from `boot_report_capture_crash` - one small write per
  boot. The **last reason** + running **panic total** (`boot_report_panic_total()`)
  are written only on abnormal boots, so panic-path flash wear stays negligible.
  The RTC counter carries the consecutive-loop state itself; NVS holds the
  lifetime totals. A retained valid dump remains visible after a clean restart
  but is not counted as another abnormal reset. Only the current reset reason
  increments the panic/abnormal-reset total. Previously inflated totals are
  preserved; this change does not erase diagnostic history. Missing NVS counters
  start at zero; other read failures abort without writes. Set/commit failures
  are logged and displayed totals retain the last successfully read values.
  NVS multi-key writes are best-effort and are not claimed to be transactional.
  Both persisted counters saturate instead of wrapping. Summary allocation/read
  failures retain the reset reason/dump presence and log the missing detail.

## Follow-ups

- Exposing the map and crash summary over `host_link` for the companion app is
  not wired yet; the on-device viewers are the current surface.


## Offline fault-injection coverage

`python3 tools/boot_report/test_boot_report.py` compiles the real module with
ASan/UBSan, both ELF-coredump-enabled and disabled configurations, and explicit
RTC/NVS/timer/heap fakes. It covers retained dumps over clean restarts, abnormal
reset sequences, saturated counters, NVS open/read/set/commit failures, failed
summary allocation/read, dump erase errors, timer allocation/arming/deletion
failures, delayed boot-loop decisions and concurrent stable-counter reads.
Required-stage failure remains visible even after the bounded boot map fills.
These tests do not exercise real reset hardware, flash writes or a watchdog
panic. The runner also exercises production P4/C5 monitors, the P4 kernel, bounded boot
audio, I2S writes and factory-reset paths with explicit scheduler/HAL/decoder
fakes. Monitor failures enter panic recovery, preserving abnormal-reset
classification and the configured core dump instead of a clean software restart.
These source-level checks complete #153 at the user-requested code boundary;
physical watchdog firing, flash-dump integrity and device recovery remain unverified.
