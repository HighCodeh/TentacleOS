# System Monitor (`sys_monitor`)

Background health task for `firmware_p4`. It samples every running task's stack
headroom on a fixed interval and **observes and reports** problems. It never
kills tasks.

- **Location:** `components/Core/sys_monitor.c`
- **Task:** `SysMonitor`, priority `SYS_PRIO_MONITOR` (1), pinned to
  `SYS_CORE_RADIO` (core 0). See [sys_prio](../sys_prio/README.md).
- **Start:** `sys_monitor_start(bool is_verbose)` from `kernel_init`.

## What it does each cycle (2 s)

1. Optionally logs heap stats (internal / PSRAM / free) when verbose.
2. Walks `uxTaskGetSystemState()` and flags any task whose stack high-water mark
   is below `CRITICAL_STACK_THRESHOLD` (256 B free).
3. For each flagged task: logs a warning, and shows **one** UI warning per streak
   via `safeguard_alert()` (which takes the LVGL lock - it never touches LVGL
   without it).
4. Tracks how many **consecutive** cycles each task stays critical. Only after
   `STACK_ESCALATE_CYCLES` (5, so ~10 s) does it perform a **controlled restart**
   (`esp_restart()`), preceded by a log, a UI alert and a short grace delay.
5. Tasks that recover or exit drop out of the table, so a fresh dip starts a new
   streak instead of inheriting a stale one.
6. It is also the **Task Watchdog heartbeat** (`esp_task_wdt_add`/`reset` each
   cycle) and the **UI render supervisor**: it polls `ui_render_beat()` (a beat
   bumped by an `lv_timer` inside the LVGL task) and, if the beat stalls for a
   couple of cycles, does the same controlled restart. This replaces the old
   "watch a task called `UI Task` by name" logic - it watches the renderer's
   actual progress, not a task name.

`usStackHighWaterMark` is monotonic (it records the lowest free stack ever seen,
and never rises again), so a sustained streak means the task is alive and running
with dangerously little headroom - not a transient spike. A long-lived task that
legitimately sits under the threshold will eventually trigger the restart; that
is the correct signal that its stack is too small and needs fixing.

## What it must never do

**It never calls `vTaskDelete` on another task.** Deleting a task in the middle
of an I2C or SPI transaction leaks the peripheral's bus mutex: the driver stays
locked forever and no other task can use that bus until reboot. On SPI3 (shared
with the display) that can take the screen down too. A tight stack must not
become a dead peripheral.

If a subsystem genuinely needs to be force-torn-down, its owner must expose a
`*_abort()` that releases its own bus/DMA/mutex/handles first; the monitor calls
that, it does not reach into the task. Until such an API exists, the only
escalation is the controlled restart above.

## Tunables (`sys_monitor.c`)

| Macro | Default | Meaning |
|-------|---------|---------|
| `MONITOR_INTERVAL_MS` | 2000 | Sampling period |
| `CRITICAL_STACK_THRESHOLD` | 256 | Free-stack floor (bytes) that flags a task |
| `STACK_ESCALATE_CYCLES` | 5 | Consecutive critical cycles before a controlled restart |
| `STACK_WATCH_MAX` | 8 | Distinct critical tasks tracked for persistence |
| `REBOOT_GRACE_MS` | 1500 | Delay after the alert so it renders / logs flush |

> The controlled restart has a `TODO(item 31)` hook: once the graceful shutdown
> path exists it should run there so the filesystem and radios flush their state
> before `esp_restart()`.

---

## ESP32-C5 (`firmware_c5`)

The C5 runs the same observe / report / escalate monitor
(`firmware_c5/components/Core/sys_monitor.c`), ported from the P4. Differences:

- **Headless, so "report" is log-only.** The C5 `safeguard_alert()` just logs
  (there is no UI), so low-stack warnings and the recovery notice go to the log
  (which the host link forwards to the P4). Everything else - the per-task
  persistence table, `STACK_ESCALATE_CYCLES`, the controlled `esp_restart()` - is
  identical.
- **Entry point is `sys_monitor(bool show_ram_logs)`**, started from
  `kernel_init`, and the task runs at `SYS_PRIO_MONITOR` on the single core.
- The bus most dangerous to strand with a stray `vTaskDelete` here is the **SPI
  bridge to the P4**, which is exactly why the C5 monitor no longer kills tasks.
- **The monitor loop is the C5 watchdog heartbeat** (the P4 uses its UI task for
  this). It subscribes with `esp_task_wdt_add(NULL)` and calls
  `esp_task_wdt_reset()` each cycle, so with `CONFIG_ESP_TASK_WDT_PANIC=y` a
  stuck monitor - or a task that starves the single core past the 5 s timeout -
  reboots instead of only logging a warning.
