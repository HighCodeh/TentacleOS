# Task Priority & Core Affinity (`sys_prio.h`)

Single source of truth for FreeRTOS task priority and core assignment on the
dual-core ESP32-P4 (`firmware_p4`). Every `xTaskCreate*` site draws its priority
and core from here instead of hard-coded numbers, so the whole scheduling policy
lives in one file.

- **Location:** `components/Drivers/sys_prio/include/sys_prio.h`
- **Why `Drivers`:** it is the one component every task-creating module already
  depends on (Applications, Service and Core all require Drivers), so the header
  is visible everywhere without adding a dependency edge. Putting it in `Core`
  would add a REQUIRES edge that reorders ESP-IDF's cyclic component link (there
  is no `--start-group`; archives are repeated) and breaks Service->Applications
  symbol resolution.

## Priority bands

Higher number = higher priority (FreeRTOS convention).

| Macro | Prio | Use |
|-------|------|-----|
| `SYS_PRIO_REALTIME` | 10 | Deferred ISR / hard real-time (radio IRQ) |
| `SYS_PRIO_RENDER` | 6 | LVGL renderer only |
| `SYS_PRIO_SERVICE_HI` | 5 | Latency-sensitive services (host link, radio rx/tx, streaming) |
| `SYS_PRIO_SERVICE_LO` | 4 | Regular services (media playback, capture, UI helpers) |
| `SYS_PRIO_BACKGROUND` | 3 | Periodic polling, logging, telemetry |
| `SYS_PRIO_BACKGROUND_LO` | 2 | Lowest non-idle background work |
| `SYS_PRIO_MONITOR` | 1 | Health monitor |

## Core affinity

| Macro | Core | Runs |
|-------|------|------|
| `SYS_CORE_UI` | 1 | LVGL renderer + everything that feeds the screen (media, capture, UI helpers) |
| `SYS_CORE_RADIO` | 0 | Radios, host link, bridge, storage, monitor |
| `SYS_CORE_ANY` | - | `tskNO_AFFINITY` (let the scheduler place it) |

The renderer runs at priority 6 pinned to core 1; radios and USB streaming run
on core 0. Keeping the two apart removes the UI jank that used to happen when a
radio scan or USB stream landed on the render core.

## Rules

- Always pin: use `xTaskCreatePinnedToCore` (or `xTaskCreateStaticPinnedToCore`),
  never the unpinned `xTaskCreate` / `xTaskCreateStatic`.
- Never pass a raw priority number or a private `#define`; use a `SYS_PRIO_*` /
  `SYS_CORE_*` macro.
- Screen background work (scans, captures) goes in its own task pinned per this
  policy, not on the UI thread. See [ui](../ui/README.md#long-running-work-and-the-watchdog).

See [Coding Standards - Concurrency](../../CODING_STANDARDS.md) for the full rule.

---

## ESP32-C5 (`firmware_c5`)

The C5 has its own `sys_prio.h` at
`firmware_c5/components/Drivers/sys_prio/include/` (same placement rationale as
the P4). It is single-core (`CONFIG_FREERTOS_UNICORE=y`) and headless, so it
keeps the same priority bands but **drops the render band and the UI/radio core
split** - every task runs on the one core.

- `SYS_PRIO_REALTIME` (10): the SPI bridge to the P4.
- `SYS_PRIO_SERVICE_HI` (5): radio and app tasks (Wi-Fi/BLE ops, scanners, DNS,
  OTA, session watchdog, mesh TCP, channel hopper).
- `SYS_PRIO_SERVICE_LO` (4): host-link logging (`c5_log`).
- `SYS_PRIO_MONITOR` (1): the system monitor.
- Core macros are `SYS_CORE_MAIN` (0, the only core) and `SYS_CORE_ANY`.

The port centralized every C5 task priority without changing any numeric value.
