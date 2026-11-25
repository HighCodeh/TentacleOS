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

# ui_manager

The integrated High Boy build uses `-Os` for theme loading and the Home, Menu,
Connect Wi-Fi, NFC presentation and Settings screens to fit the existing P4
application slots. This changes compiler sizing only: no screen or feature is
removed. NFC protocol/driver code, radio processing, audio and game engines keep
their existing optimization settings. Muse/Grok layout retains its previous
localized LTO policy. Physical UI timing still needs acceptance testing.

Wi-Fi > **ESP SCAN** uses isolated black pages for starting, scanning and viewing
one full-screen result at a time. The existing waves component animates the scan;
results show a circular heuristic-percent indicator, SSID, MAC, OUI, manufacturer
and RSSI with colored text. Other screens retain their existing styling.
Its P4 proxy, session worker and screen
use `-Oz` to fit the same app slots; P4 remains on IDF v5.5.3. No LVGL callback
performs SPI calls. It now reuses one existing AP scan and ranks BSSID/OUI/RSSI
snapshots locally, with no monitor-mode session or duration selector. The screen
close hook requests local cancellation and removes the refresh timer; the worker
waits for the shared scan to finish before exiting. See the
[current ESP SCAN behavior](../wifi/quick_scan.md).

Step-by-step process for adding a new screen (feature) to TentacleOS using the
`ui_manager` architecture. Source of truth: `ui/ui_manager.c`,
`ui/include/ui_manager.h`, `ui/include/ui_metrics.h`, `ui/include/ui_theme.h`,
and the screens under `ui/screens/`.

**Example** used: a fictional **Bluetooth (BLE)** menu screen.

### 1. Add a screen id
The `ui_manager` identifies every screen by an enum value.

**File:** `ui/include/ui_manager.h`
Add a new identifier to `screen_id_t`:
```c
typedef enum {
    SCREEN_NONE,
    SCREEN_HOME,
    SCREEN_MENU,
    SCREEN_WIFI_MENU,
    // ...
    SCREEN_BLE_MENU, // <--- NEW ID
} screen_id_t;
```

### 2. Register the open function in the dispatch table
Routing is a dispatch table, not a `switch` inside `ui_switch_screen`.
`screen_open_fn(screen_id_t)` maps an id to the function that builds and loads
the screen; `ui_switch_screen` looks the id up there. If it returns `NULL` the
screen is treated as unavailable and the manager stays put.

**File:** `ui/ui_manager.c`
1. Include the screen header (created in Step 4):
```c
#include "ui_ble_menu.h"
```

2. Add a `case` to `screen_open_fn()`:
```c
static ui_open_fn_t screen_open_fn(screen_id_t s) {
  switch (s) {
    // ... other cases ...
    case SCREEN_BLE_MENU:
      return ui_ble_menu_open; // <--- NEW ROUTE
    default:
      return NULL;
  }
}
```

The NFC Classic emulation overlay uses a static card, field rings and status
while listening; packet counters and decorative animations would compete with
NFC response deadlines on the shared display SPI bus. BACK restores the card
picker and its animations. See [Classic transport](../nfc/README.md#classic-emulation-transport).

### 3. If the screen owns hardware or a task, register a stop hook
Screens that start a radio, a worker task, or a media player must register a
stop function in `screen_close_fn()`. `ui_switch_screen` calls it on the
outgoing screen before tearing it down, so the resource is always released on
navigation. Current examples: `SCREEN_SUBGHZ_READ` -> `subghz_receiver_stop`,
`SCREEN_NFC_READ` / `SCREEN_NFC_EMULATE` -> `nfc_manager_stop`,
`SCREEN_WAV_PLAYER` -> `ui_wav_player_stop`, `SCREEN_MP3_PLAYER` ->
`ui_mp3_player_stop`, `SCREEN_IMAGE_VIEWER` -> `ui_image_viewer_stop`,
`SCREEN_USB_STORAGE` -> `ui_usb_storage_stop`.

```c
static ui_close_fn_t screen_close_fn(screen_id_t s) {
  switch (s) {
    // ... other cases ...
    case SCREEN_BLE_READ:
      return ble_scanner_stop; // <--- release the radio/task on leave
    default:
      return NULL;
  }
}
```

This supersedes the old advice of calling `ble_init()` / `ble_deinit()` inside
`ui_switch_screen`. A plain menu that owns no hardware needs no close hook.

`menu_ui.c`'s `ensure_radio_on()` enables Wi-Fi when entering its area. Opening
the Bluetooth menu does not enable BLE or save a preference; BLE operations
acquire the radio when started and release it through their close hooks.
**Settings → Connections** contains Wi-Fi and Bluetooth submenus alongside
USB Native. Each radio submenu holds drafts until Save & apply; name/password
editing uses the contextual keyboard. Wi-Fi also links to Connect to network.
That screen reuses saved Wi-Fi credentials and offers password editing after a
failed join. Its synchronized completion timer is closed before navigation, so
a late reply cannot open the keyboard on another screen. See
[Wi-Fi connections](../wifi/README.md#connection--management).
Holding UP opens the quick panel only on Home and the main coverflow menu,
including Wi-Fi and Bluetooth on/off switches backed by the same service.
Submenus and lists retain held-UP navigation without opening the panel.
It opens on brightness; tap UP to select the radio
row, LEFT/RIGHT to choose a shortcut and OK to apply it. Filled icons indicate
the applied preference; pending operations dim both radio controls. Compact spacing
keeps the panel inside the landscape display. Screen close hooks release the
keyboard/timer; a Service-owned worker completes independently after BACK.
Progress and apply/save errors appear on screen. See
[connection settings](../connection_settings/README.md) for fields and lifecycle.

Wi-Fi → Port Scan opens target/port configuration before starting. It supports
live gateway/subnet discovery, IPv4 ranges, CIDR and custom port lists/ranges, plus
TCP/UDP/Both selection and optional Ping first (off by default). Scan opens a separate execution view showing
current IP, phase, counters, progress and a flat IP:port/banner list. `PING xN`
shows discovery of up to eight addresses and the first IP in that batch.
With Ping first, **1/2 DISCOVERY** completes the entire interval and shows
probed/total IPs plus responders; **2/2 TCP SCAN** or **2/2 UDP SCAN** then scans those hosts one at a
time and shows completed/responding hosts and current ports. The bar measures
each phase separately. Cancelling discovery starts no port work; cancelling ports
keeps the partial port results. Its first BACK
stops in place; the second returns to setup. Themed layout supports both orientations.
UDP results show OPEN, OPEN|FILTERED (silence) or FILTERED (ICMP rejection).
Only OPEN increments the open counter; uncertainty and filtering have separate
counters. Saved rows retain protocol and state, with full labels in details.
Its Service worker publishes progress and saves all collected results to an
SD archive keyed by the scanned interval, keeping a bounded live preview. A new
scan of that interval replaces the old one, retaining the new partial data on
cancellation. Saved scans opens networks/intervals, then unique IPs, then their
ports and banner details. Results opens the current scan's IP list. LEFT/RIGHT
pages directly, without Previous/Next rows. The IP list includes Delete scan with
confirmation and asynchronous deletion; BACK returns one level. Eight entries
are read per page through a cancellable Service worker, including archives from
previous boots. SD errors appear on screen. The close hook also cancels pending reads. Neither SPI nor
file I/O runs in LVGL. See [port scanner](../port_scanner/README.md) for the full workflow.

### 4. Create the screen source
Create the files under `ui/screens/bluetooth/`.

**Header:** `ui/screens/bluetooth/include/ui_ble_menu.h`
```c
#ifndef UI_BLE_MENU_H
#define UI_BLE_MENU_H
void ui_ble_menu_open(void);
#endif
```

**Source:** `ui/screens/bluetooth/ui_ble_menu.c`. The current template:

```c
#include "ui_ble_menu.h"

#include "esp_log.h"

#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static const char *TAG = "UI_BLE_MENU";

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

// Event-driven input: one debounced event at a time. The central pump only
// calls this while input is unlocked and no modal overlay is up. UP/DOWN also
// act on REPEAT for held auto-scroll; actions use PRESS only.
static void ble_menu_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav) menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press) { /* enter the selected item */ }
      break;
    default:
      break;
  }
}

void ui_ble_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "BLUETOOTH", "/assets/icons/bluetooth.bin");
  // ... add items ...

  ui_input_set_screen_handler(ble_menu_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
```

Points that differ from older screens - all required for new screens:

- **Input is event-driven.** Register a handler with
  `ui_input_set_screen_handler(handler, ctx)`; it receives an `input_event_t`
  (`ev->button` in `INPUT_BTN_UP..BACK`, `ev->action` in
  `INPUT_ACTION_PRESS` / `RELEASE` / `LONG_PRESS` / `REPEAT`, both from
  `input_manager.h`). No `LV_EVENT_KEY` callback, no `lv_event_get_key`, no
  manual `main_group` focus, no `s_*_last` edge bookkeeping. The handler is
  cleared for you on the next screen switch. See
  [input-migration.md](input-migration.md) for the full pattern and gotchas.
- **Load the screen owned.** Use `ui_screen_load_owned(&s_screen, scr)` instead
  of raw `lv_screen_load`. When the object is freed on navigation, the slot is
  set back to `NULL`, so the screen never double-frees or dereferences a stale
  pointer. Do not manually `lv_obj_del` the previous screen in the open
  function beyond the guarded self-cleanup shown above.
- **Be rotation-aware.** Never hardcode `LCD_H_RES` / `LCD_V_RES` for layout;
  those are fixed panel constants. Use `ui_screen_w()` / `ui_screen_h()` from
  `ui_metrics.h` (they follow the live rotation). If the screen must reflow
  after a rotation change, call `ui_relayout_current_screen()`. When a component
  polls button levels directly, use `ui_nav_pressed(logical)` so it navigates
  correctly in landscape.
- **Schedule onto the UI thread from workers.** Any worker/radio task that
  needs to touch LVGL must marshal through `ui_async_call()` (the required
  wrapper around `lv_async_call`; it takes the UI lock first). Never call LVGL
  directly from another task.
- **Styling uses the theme.** Pull colors from `current_theme` in
  `ui_theme.h` (`screen_base`, `bg_primary`, `text_main`, `border_accent`,
  ...). For a protocol screen, set the active protocol with
  `ui_theme_set_protocol(PROTOCOL_BLE)` and read the accent with
  `ui_theme_get_accent()`, or use the per-protocol fields directly
  (`current_theme.protocol_ble`, `protocol_nfc`, `protocol_wifi`,
  `protocol_subghz`, `protocol_rfid`, `protocol_ir`, `protocol_lora`). Do not
  hardcode `lv_color_black()` / `lv_color_white()`.

### 5. Link from the main menu
The main menu (`ui/screens/menu/menu_ui.c`) is a data table: each
`menu_ui_item_t` carries a `target` of type `screen_id_t`. Add (or point) an
entry at the new id:
```c
{"BLUETOOTH",
 { /* icon frames */ },
 BASE_FRAMES, {NULL}, {NULL},
 SCREEN_BLE_MENU}, // <--- target
```
Selecting the item calls `ensure_radio_on(target)` and then
`ui_switch_screen(target)`; you do not write a per-item `case`.

### 6. Build system (CMake)
**File:** `components/Applications/CMakeLists.txt` (this is the `Applications`
component; there is no separate CMakeLists under `ui/`).

Sources are picked up automatically: `file(GLOB_RECURSE UI_SRCS "ui/*.c")`
globs every `.c` under `ui/`, so a new screen source compiles without editing
the SRCS list. Because it is a glob, **adding a new `.c` needs a reconfigure**
(`idf.py reconfigure`, or a clean build) to re-run it.

The only hand-maintained part is `INCLUDE_DIRS`: when you introduce a brand-new
screen *area* (a new folder with its own `include/`), add its include dir:
```cmake
  # --- ui screens ---
  "ui/screens/bluetooth/include" # note: screens/bluetooth/, there is no screens/ble/
```
An existing area (like `bluetooth`) already has its include dir listed, so a
new screen inside it needs nothing here.

---

## Long-running work and the watchdog

Screens run on the UI thread under the LVGL lock (`ui_acquire` / `ui_release`).
Two hard rules keep the device from freezing or rebooting:

1. **Always check `ui_acquire()`.** It uses a finite 1000 ms timeout and returns
   `false` if the lock is held elsewhere. The screen template already guards
   every access with `if (ui_acquire()) { ... ui_release(); }` - keep it that
   way. Never assume the lock was taken.

2. **Never block, sleep or loop while holding the lock, and never run a busy
   loop on the UI thread.** Rendering liveness is supervised by `sys_monitor`: a
   lightweight `lv_timer` bumps a render-progress beat (`ui_render_beat()`), and
   if the beat stalls - a lock held too long, a runaway callback, or a frozen
   LVGL task - the monitor does a controlled restart. Separately, a task that
   spins a core past 5 s trips the idle Task Watchdog and panics
   (`CONFIG_ESP_TASK_WDT_PANIC=y`). Either way the board recovers instead of
   freezing.

**Pattern for scans / captures / anything that takes more than a frame:** run it
in its own FreeRTOS task, not on the UI thread. The SubGhz receiver is the
reference - `subghz_rx_task` blocks on a queue (so it yields the CPU), never
touches the LVGL lock, and the screen (`subghz_read_ui`) only uses `lv_timer`
callbacks to refresh. Push results from the worker task back to the screen with
`lv_async_call` or an `lv_timer`; both run inside the LVGL task, already under
the lock. Create the worker with `xTaskCreatePinnedToCore` using a priority and
core from [`sys_prio.h`](../sys_prio/README.md) (radios / IO on core 0, UI-side
helpers on core 1).

## Input: event-driven handlers

Screens no longer poll the buttons with their own `lv_timer`. Input comes from
[`input_manager`](../input_manager/README.md) as debounced events, dispatched by
a single central pump to the active screen's handler.

To add input to a screen:

1. Write a handler `static void my_input(const input_event_t *ev, void *ctx)`.
   The event carries `ev->button` (`INPUT_BTN_UP..BACK`) and `ev->action`
   (`PRESS` / `RELEASE` / `LONG_PRESS` / `REPEAT`). `PRESS` is the debounced edge,
   so there is no `s_*_last` bookkeeping.
2. Register it at the end of your `*_open()` with
   `ui_input_set_screen_handler(my_input, NULL)`.

That is all: no timer to create or delete, no `ui_input_is_locked()` /
`msgbox_is_open()` guards (the pump handles them), and the handler is cleared for
you on the next screen switch. Use `REPEAT` for held auto-scroll and
`input_is_down(button)` when you need a continuous held state (games).

See `nfc_menu_ui.c` for the reference handler, and
[input-migration.md](input-migration.md) for the full pattern and gotchas. Legacy coverflow menus still use the LVGL keypad; the central guard disables
that path whenever an event handler or modal owns input. Octopet uses the same
event handler contract. Games and the BLE mouse can still poll continuous
controls, but their BACK action belongs to the central handler and their polling
is blocked during modal dialogs and consumed gestures.

### Gesture ownership and BACK

See the [navigation audit](navigation-audit.md) for route coverage, corrected
parent destinations and the remaining lifecycle findings.

Home opens the selected shortcut with OK and the main menu with DOWN. Features
return through their declared
parents. Screen changes do not retain a cross-application history, previews or
keyboard drafts. Settings remembers the selected row separately for its main
and Audio & Haptics lists; its firmware grid, Infrared and Interface menus
also retain selection when returning from a child.

The UI dispatches a short physical BACK as one `PRESS` **on release**, preserving
existing screen handlers and their parent destinations. BACK repeats and the
800 ms long-press event never navigate. Holding BACK for five seconds belongs to
the shutdown policy and does not perform a short BACK afterward.

Handler changes, navigation locks, keyboard transitions and message-box
transitions consume queued input and held buttons through release. Message boxes
also lock input during their closing animation. The LVGL keypad waits for release
instead of generating a click on the previously focused control. The quick panel
stops reading actions while a message box is open.

Run `python3 tools/highboy/tests/test_ui_navigation.py` for host regressions of
the production router (short/held BACK, modal close, queued input, keyboard and
legacy-menu ownership). Hardware validation should include Connections → Settings,
Wi-Fi scan → results/configuration, and cancelling shutdown from each screen.

## Screen power policy (auto-dim / sleep)

`components/ui/components/power_policy` owns the always-on display power
behaviour in a single `lv_timer`, started once from `ui_init` under the LVGL
lock. It reads `input_last_activity_ms()` (from `input_manager`) and the
`screen` config (`g_config_screen.auto_lock_seconds`, `auto_dim`):

- After `auto_lock_seconds` of no input it sets the backlight to zero and calls
  `lcd_display_sleep(true)` to cut the panel. When `auto_dim` is set it first
  dims to a low level a few seconds before sleeping. Later inactivity ticks
  cannot reapply dim brightness while asleep: the backlight remains off and
  the [charging LED cadence](../led/README.md#p4-ownership-and-feedback) remains
  eligible until a wake condition occurs.
- The user's configured brightness is restored on the next input, so auto-dim
  never overwrites it. Transient changes use
  `lcd_apply_brightness` (no persist); see [st7789](../st7789/README.md).
- Any input wakes the screen. `power_policy_is_asleep()` gates the central input
  router so the wake press only wakes the display instead of also acting on the
  underlying screen.

The display settings screen writes `auto_lock_seconds` / `auto_dim` / brightness
through `tos_config`, which is the sole writer of the `screen` config file.

`python3 tools/highboy/tests/test_power_feedback.py` exercises the production
inactivity functions together with backlight publication and LED selection on
the host. It verifies repeated sleep ticks and charge pulses across two periods,
with and without auto-dim, plus brightness restoration and the wake-press guard.

## Execution flow summary
1. User selects **Bluetooth** in the main menu.
2. The menu resolves the item's `target` (`SCREEN_BLE_MENU`), calls
   `ensure_radio_on(target)` (no radio change for the BLE menu), then
   `ui_switch_screen(SCREEN_BLE_MENU)`.
3. `ui_switch_screen`:
  - Looks the id up in `screen_open_fn()`; stays put if it is `NULL`.
  - Calls the outgoing screen's `screen_close_fn()` stop hook, if any.
  - Clears the previous screen (and the old input handler).
  - Calls the resolved open function, `ui_ble_menu_open()`.
4. `ui_ble_menu_open`:
  - Builds the screen objects (theme colors, rotation-aware layout).
  - Registers its input handler with `ui_input_set_screen_handler()`.
  - Loads the screen with `ui_screen_load_owned(&s_screen, scr)`.

**Done: the new screen is integrated, safe and navigable.**

## Integrated background workers

Affected screen and boot worker creation sites use `SYS_PRIO_*` and
`SYS_CORE_*` directly. Native RFID cancellation/results are synchronized and
field cleanup remains with the reader task; see the
[RF frontend reference](../rf_frontend/README.md). The native Muse PTT owner and
its recording/control split are documented in [Muse](../muse/README.md).


## Native P4 application update

The P4 Update screen now checks the actual SD image and installed firmware version.
A pinned native worker performs the SD check/update and publishes bounded status;
the UI timer only renders it. The screen has no simulated version, simulated
progress or UI-triggered reboot. Only successful native OTA selects/reboots the
image. Reopen/delete does not leave a worker with LVGL pointers. See the
[OTA behavior and validation](../ota/README.md#native-p4-update-screen-and-failure-handling).

## Black page backgrounds

All built-in pages use black backgrounds, including the shared header and footers, menu
surface, built-in arcade games and IMU canvas. Theme loading normalizes
`screen_base` and `bg_primary` to black after reading flash or SD palettes, so
existing SD themes do not restore the old purple page backgrounds. Cards,
buttons, selection fills, protocol accents and media content keep their colors.

Main and secondary text colors below luminance 96 are replaced with white and
light gray respectively. Owned screens inherit the main text color for labels
without an explicit color. Dark text on filled badges/buttons retains its
explicit style; their contrasting fills remain unchanged. An app-only P4 update
applies this policy without rewriting theme files or rebuilding assets.

Home and the Apps library take their card, text, focus and icon colors from the
active theme. The original mascot and media artwork retain their source colors.
Home uses the event input path. Settings is its permanent first shortcut and
the only default; the remaining shortcuts come from manually pinned main-menu
apps. Hold OK on a main-menu app to pin/unpin it; Settings cannot be unpinned.
LEFT/RIGHT selects shortcuts, including additional pages, and OK opens the
selection. No recent-app history is recorded or displayed. Portrait shows the
date, mascot and shortcuts vertically; landscape places the mascot beside the
date and shortcuts. Its 30-second date refresh is stopped by the central close
hook
and an idempotent screen-deletion callback. No binary font is loaded just for the
Home date. Home leaves the breadcrumb title empty; the redundant HIGH BOY caption is not shown below the status bar.

The shared 22-pixel footer has a bounded, centered hint label. Long hints scroll
inside its margins instead of extending off-screen; normal menu hints are kept
short. Menu row labels are constrained to their remaining flex width. Toggles,
selectors and intensity controls occupy layout space rather than floating over
the name. Selected long names scroll within their row; unselected names use an
ellipsis. Standard menu icons follow the theme accent, and the scroll indicator
is hidden when the list fits (including an empty list). The marker is a themed
10x18-pixel box over a two-pixel dotted track. Its position is mapped from the
list's actual animated scroll destination, clamped to the track ends, rather
than the selected item number. Custom content uses the same bounded pixel mapping. Apps descriptions are bounded above the footer in both orientations.
The Apps close hook detaches and releases cover/registry images before leaving;
a delayed DELETE from an earlier screen cannot release a new screen's registry.

Snake and Breakout derive their playfield from the logical display size, including
landscape. Their UI overlays use theme text/surface colors; gameplay objects keep
their semantic game colors. Central stop hooks remove their timers synchronously,
and gameplay does not advance while system dialogs or navigation locks own input.

The display draw buffer remains RGB565. Home/Apps cards and shared menu gradients
use an ordered 8x8 dither at final 5/6/5-bit quantization instead of uniform-color
bands. A 32-pixel-wide RGB565 tile is generated once in checked PSRAM (at most
20,480 pixel bytes per object), repeated horizontally and clipped by LVGL to the
card radius. Each object releases its image cache entry and allocation on deletion.
No timer, per-frame generation or internal-RAM fallback is added. Equal colors use
no tile; allocation failure leaves a solid themed background. Menus use opaque
rows so alpha blending does not quantize the dither a second time. Rebuild screens
on theme/orientation changes; this helper is for newly created, fixed-height objects.
Artwork and other custom gradient renderers are not changed by this helper.
`tools/highboy/tests/test_ui_gradient.py` checks dark-ramp average color, endpoints,
height bounds, PSRAM allocation failure, registration failure and deletion ownership.

## Companion connection screens

Companion and the PC Monitor waiting screen display a centered QR containing
only the complete 64-character hexadecimal PSK, without a URI, prefix or newline.
OK toggles between QR and four readable text rows. PC Monitor replaces the QR
with telemetry after authentication and restores it after disconnection. Both use
[automatic PSK authentication](../host_link/pairing.md),
without a comparison PIN or OK approval. Screen close hooks release timers and
an unlinked BLE relay started by that screen, while authenticated connections
survive navigation. When BLE startup fails, PC Monitor's OK retries that relay.
USB authentication does not require either screen to remain open.

The shared `components/psk_qr` view uses the parent's content bounds between
chrome bars. It renders version-5 QR modules at an integer scale, with four white
modules of quiet space on every side: 180 px in 240x320 and 135 px in 320x240.
The QR is static, with black/white scanner colors centralized in `ui_semantic.h`;
labels and screen surfaces follow the selected theme, including SD themes.
Theme/orientation rebuilds release the old view and generate a fresh one.

QR scratch and 1-bit pixels allocate explicitly in PSRAM. Scratch is released
after encoding; pixel storage belongs to the canvas and is released on view
cleanup/deletion. The screen close hooks clean the view before navigation.
Failure to allocate/render a QR leaves the complete text key available and
reports an error. Missing PSK and timer errors keep BACK usable; radio startup
requires the status timer. No QR or PSK file is written to storage.

Use the host LVGL build prepared by `tools/highboy/tests/test_keyboard_ui.py`.
`tools/highboy/tests/test_psk_qr_ui.py <lvgl-host-build>` renders the production
widget and both screens against host LVGL, checking bounds, centering, quiet
space, text toggling, themes, both orientations, allocation failures and cleanup.
Its PPM captures use a fixed test PSK and can be decoded independently; they are
not evidence of scanning the physical LCD.

## Theme path limits

Selecting a different theme asks for APPLY or CANCEL before changing the stored
theme. BACK/cancel keeps the current configuration and does not reboot. APPLY
checks the save result before restarting; a failed write restores the in-memory
theme name and leaves the selector usable.

SD theme names must fit the persistent 32-byte theme field (31 bytes plus NUL).
The selector skips longer names with a warning instead of saving a shortened
name that could identify another directory. Theme file and asset paths use
checked joins. `ui_theme_load_from_name()` returns `ESP_ERR_INVALID_SIZE` for an
oversized name without reading files, changing colors or unloading assets.
The existing flash fallback for a missing valid SD theme is retained.
Companion theme commands reject oversized names and embedded NUL bytes before
saving or scheduling a reload; an optional final NUL is accepted.

## microSD filesystem and formatting

The card-connected dialog reads the mounted filesystem type from the VFS, just
as Storage and SD Health do; it does not assume FAT32. Under Settings > Storage,
**Format SD: exFAT** explicitly selects exFAT, while **Format SD (auto)** lets
FatFS select the type. Both require confirmation that all files will be erased.

Formatting runs in a background worker. Its overlay displays the current FatFS
stage and a determinate 0–100% bar. Completion is weighted by filesystem stages,
with sector-count progress within exFAT's bitmap/FAT loops; it is not elapsed-time
progress or a full-card erase percentage. 100% requires successful format,
remount and volume labeling. CANCEL (touch, OK or BACK) requests cooperative
cancellation between writes. The current SD command must finish/timeout first;
the worker is never killed and the card/context remain owned until cleanup.
The overlay says "Cancelling..." until that finishes and reports cancellation
separately from failure/success. An interrupted format may leave the SD unreadable.
Other storage actions remain blocked while the worker owns it. Overlay deletion
and screen recreation do not leave worker references to LVGL objects.

The built-in games menu no longer includes the experimental 3D scene. Its
renderer, embedded mesh/texture, host service and startup hooks are removed.
Game Boy, Doom and external app discovery retain their existing behavior.

## Coverflow navigation timing

The main carousel transitions in 160 ms; the theme carousel uses 180 ms. The
main menu waits until its card animations finish before accepting another
directional event, opening an app or changing a favorite. Events received during movement are discarded,
not queued; the guard checks active LVGL animations instead of a fixed delay.
BACK remains available during movement and preload. Shared content fades use
120 ms. The 80 ms native-entry guard consumes the initiating held gesture until
release. Worker shutdown can
still take longer than the visual transition. The input regression suite is
`tools/highboy/tests/test_ui_transitions.py`.
`test_menu_preload.py` also verifies the animation guard, BACK during movement,
and idle navigation while preserving decoder deduplication, preloading and cleanup.

On opening the menu, the visible images load first and navigation is immediately
available. A 20 ms UI timer preloads one remaining item/frame pair per idle tick;
it pauses during item animations to avoid competing with rendering. Preloading
never resets the selected item or its pose. All distinct icon and base frames
are decoded and pinned in the LVGL image cache for the menu's lifetime, including
the side views. Decoder handles live in PSRAM; shared bases and repeated glyphs
are pinned only once. The central close hook cancels pending preload and releases every
handle before navigation; a guarded DELETE callback is an idempotent fallback.
This permits cache eviction without letting an old root release a new preload. Allocation or decoding failure releases the pins
and retains on-demand loading. Rapid navigation to a not-yet-preloaded image can
still require a synchronous load. Display DMA buffers and the separate coverflow
preview are unchanged.

Larger packaged frames use lossless LZ4 BIN encoding. The asset manager decodes
these with LVGL's bundled LZ4 library into the same ARGB8888 cache buffers; raw
flash/SD images remain supported. A checked PSRAM scratch buffer exists only
during decompression. Invalid lengths, unsupported compression and allocation
failures reject the image without retaining that buffer. See
[asset generation and deployment](../storage_assets/README.md#v203-p4-assets-build-and-flash).
`test_assets_compression.py <native-lvgl-build-directory>`, run with the image
converter's Python environment, compares all generated frame pixels, exercises
malformed files, SD preloads, cache eviction and allocation failure with real LVGL.

### USB Storage with an unavailable SD

USB exit no longer depends on successfully mounting the card. After USB I/O has
stopped, the backend attempts one bounded CMD12 recovery if the first mount
fails. A persistent failure displays `USB stopped; SD unavailable`; BACK returns
to Home without launching another exit worker. The card remains quarantined and
Storage cannot re-expose it until restart. Normal host eject confirmation and
successful return to Files are unchanged.

## Live battery display

Header, dropdown, Battery and Power consume the battery service. A qualified
gauge supplies percent when available; otherwise valid voltage supplies the
shared filtered estimate, displayed as `~NN%`. Unavailable telemetry shows `--`.
The header and its static snapshots never show voltage beside the battery icon;
when no percentage is available they show `--`. Voltage remains in the battery
details and quick panel.
The four header battery levels represent 0–24%, 25–49%, 50–74% and 75–100%.
During actual charging, an object-owned LVGL animation cycles those frames at
approximately 350 ms per frame while the percentage text retains the service value.
Pausing, finishing charge or losing telemetry restores a static icon. Lightning
means actual charging, and USB means external power. Static overlays use the
same renderer without rebinding or animating the live header. Header replacement
cancels the old animation; object deletion also releases it. The dropdown's
continuous fill remains tied to the reported percentage.

Battery settings show the estimate with "Estimated from voltage" and retain
measured voltage, net current, USB supply and nominal capacity as separate values.
The supply field shows USB voltage and the sampled input current limit on two lines;
this limit is separate from net cell current. Automatic source policy is documented
in [power management](../power_manager/README.md#usb-input-budget-on-v203).
The footer names the actual Pause/Resume action; the card shows the actual charge
state and blocked reason. The active model and nominal capacity are read-only;
LEFT/RIGHT does not change the battery profile. Use `battery profiles` and
`battery profile <id>` in the P4 console to select and persist a catalog entry.
The next refresh displays the selected profile. Block reasons come from the
service; the screen does not impose its own 3200 mV rule. Selecting the confirmed
604858 allows normal charging above 3000 mV; generic profiles retain 3200 mV.
This does not recalibrate the
voltage curve or gauge, or enable low-cell recovery. Power uses the same cached,
filtered value and queues charge requests through the service. Entering Power
does not initialize the charger or perform ADC conversions in LVGL.
Its **Battery saver** row persists the C5 idle-sleep preference (enabled by
default). Active operations veto sleep; display-off selects a longer idle window.
The [power manager](../power_manager/README.md) owns this policy and the bridge
worker applies it outside LVGL. A failed save retains the previous setting and
is shown on screen. Its explicit
Registers/I2C Scan diagnostics remain user-triggered. V2.03 low-cell alerts and
shutdown still use voltage thresholds while the percentage is estimated.

The battery refresh timer is stopped by the central close hook; a DELETE
fallback checks the owning root so deletion of an old screen cannot cancel the
new screen's timer. Screen refresh timers are released on close. USB presence alone does not imply
full charge. The service's hardware guards remain active for every UI request.
While the backlight is lit, normal charging is indicated on screen without a
continuous charging LED. Screen-off charging uses a 200 ms red pulse every
10 seconds; charge completion permits one green pulse of at most one second.
Application events and activity patterns take priority throughout their cadence.
See [LED](../led/README.md) for brightness caps and alerts, and
[battery service](../bq25896/README.md) for qualification and charge limits.


## IMU motion monitor

The monitor is in `Applications/ui/screens/imu`. It consumes snapshots and
requests from `Service/imu`; sensor transactions and calibration belong to the
service and `Drivers/qmi8658a`. The sampling worker never touches LVGL.

The entry is Settings → Audio & Haptics → Motion / IMU. OK recenters, UP
recalibrates, RIGHT toggles raw data/3D, and BACK exits. After the service exhausts
automatic recovery and stops, UP retries startup. The layout uses `ui_metrics`
for 240×320 and 320×240. Canvas dimensions and projection
scale follow the available content area, leaving header, values and footer visible.
Warm-up, calibration, stale samples, automatic recovery and failures have explicit
status text. Canvas background, axes and labels use the selected theme; the mesh
retains its material colors.

`screen_close_fn()` registers `ui_imu_monitor_stop()` before navigation teardown.
The delete event is an idempotent fallback and frees the screen-owned PSRAM model,
pixel and depth buffers. The refresh timer is created before starting the worker;
failed allocations/model loads/timer creation/start leave an error page with BACK
available, without a display hold or a newly started sensor worker.
Only a screen that started the service requests its stop, so the delete fallback
does not report the same shutdown twice. A worker that stops after a sensor error
also releases the screen's display hold. Error notifications are deduplicated;
an actual shutdown timeout leaves cleanup with the service, not the UI.

The model is `/assets/models/highboy_imu.bin`, loaded into PSRAM. Deploy the asset
with the P4 app; an app-only update does not install assets. Its format and source
mesh are verified by `python3 tools/highboy/build_imu_mesh.py --check`.

Run `python3 tools/highboy/test_imu_motion_ui.py --output /tmp/imu-ui` for actual
LVGL renders in portrait/landscape with simulated samples, controls, repeat
entry/exit, missing/corrupt models, injected allocation/start failures, retry and
display-hold release. These checks do not establish physical sensor accuracy or
on-device rendering speed.

## Responsive activity layouts

Device selection grids retain the selected item and scroll it fully into view.
Card titles keep one line: inactive long names use an ellipsis and the selected
name scrolls. Compact directional glyphs keep activity footer hints readable
within the portrait width.
IR controller keypads, Power actions, C5 actions and BLE Radio use the shared
box thumb/dotted track; About and SD Health allow UP/DOWN content scrolling.
Wi-Fi Hotspot uses the shared menu and explicit Start/Stop control. Opening it
does not enable the AP. Status reports applying, online, offline or failure;
its close hook requests stop on BACK and programmatic exits. A guarded DELETE
fallback releases its refresh timer. LoRa Telemetry uses LEFT/RIGHT
for the page and UP/DOWN for neighboring nodes, automatically revealing the
selected node. LoRa Position fits its fields/status in both orientations.
Landscape IR Raw separates its trace/carrier controls from statistics; LED
signals places its preview beside its controls. BLE/USB mouse panels, the BLE
scan radar and WAV spectrum derive their available height from live metrics.
MP4 metadata uses two columns in landscape and explicitly reports unavailable
playback instead of suggesting an unimplemented codec path.

`ui_chrome_screen_create()` shares the identical opaque root setup used by
activity screens. It removes duplicated setup code without changing screen
ownership. Presentation sources use size optimization to retain the existing
OTA partition layout; codecs and peripheral services keep their prior flags.
The read-only `uistate` console command snapshots bounded object/label geometry
under the UI lock, then prints after releasing it. It does not execute actions.
Its flags describe scrollability, visibility and hidden state; clipped children
inside scrollable content are not automatically layout defects.

## Physical-button text editor

The shared editor uses a button matrix with lower-case and upper-case QWERTY
pages: `QWERTYUIOP`, `ASDFGHJKL`, `ZXCVBNM`, then the punctuation row. Number
and symbol pages use eight columns; hex fields use a four-column 0–F map.
It covers printable ASCII; existing UTF-8 text is retained and counted by character.
Arrows move selection, OK activates a key, the arrow keys drawn on screen move
the text cursor, backspace erases, the checkmark confirms, and physical BACK
cancels without invoking the submit callback. The same controls fit 240x320 and
320x240. The screen shows a field-specific title, current count and limit.

`keyboard_open_config()` takes a copied configuration: title, initial text,
character limits and text/password/hex/number mode. Names, drafts and editable
settings are prefilled; passwords are masked. Too-short input or incomplete hex
bytes stay open with feedback. Submission detaches the old editor before calling
client code, using a checked PSRAM text copy so navigation and a callback opening
another editor cannot invalidate the submitted text or close the new editor.
BACK never saves. The stock LVGL keyboard widget is disabled in the P4 profile;
this editor uses buttonmatrix and textarea, including its own compact maps.

Run `python3 tools/highboy/tests/test_keyboard_ui.py /tmp/highboy-keyboard` to
compile the production editor against the pinned, local LVGL source. The host
suite checks both layouts, printable characters, cursor/delete, limits, masked
text, UTF-8 drafts, allocation failure, cancellation and reentrant submission.
It does not replace device button/render checks. Hotspot worker error/exit
ordering is exercised by `test_hotspot_ui.py` with mocked Wi-Fi operations.

IR Raw displays an empty-state instruction until a capture exists. Its trace now
projects the first 64 actual RMT symbols, using their durations and levels, rather
than a decorative waveform. The title identifies it as the signal start. The
projection and bounds are checked by `test_ir_raw_ui.py`; physical replay still
requires a fixture. Direct entry into an audio/video viewer without a path asks
for a file instead of reporting a corrupt file. Offline LoRa pages name the
protocol setup step; diagnostics consume the service percentage, with voltage
as a fallback when no percentage is available.

## P4 UI memory policy

`Service/lvgl/lvgl_memory.c` implements the custom LVGL allocator selected by
`CONFIG_LV_USE_CUSTOM_MALLOC`. On the PSRAM-equipped P4, all LVGL allocations,
including small objects, styles and strings, use PSRAM with no internal-memory
fallback. Display DMA buffers and radio/crypto allocations retain their explicit
capabilities. This keeps navigation and keyboard activity from consuming the
internal DMA heap needed for mesh encryption and SPI transfers. Realloc failure
preserves the original pointer. The allocator has no private pool statistics;
`lv_mem_test_core` checks the backing heap integrity. Test its policy with
`python3 tools/highboy/tests/test_ui_memory.py` and repeat simultaneous UI/radio
acceptance after changing this configuration.

On 16 November 2026, unit 0201891B passed eight chat entries with repeated
portrait/landscape changes, three console-queued MeshCore transmissions and one
transmission submitted through the actual keyboard UI. Internal free heap ended
at 61,543 bytes after the rotation sequence; no SPI/crypto allocation failure or
reset appeared in that run. The prior allocator reproduced a crypto allocation
failure during the same sequence. This is single-unit acceptance, not proof of
peer reception or long-term stability. Existing redundant standby-transition
and haptic-playback warnings remain separate from this memory fix.

## LoRa chat

The mesh home opens Chat directly. Its implementation and delivery semantics
are documented in [LoRa / Chat UI](../LoRa/README.md#chat-ui-uiscreenslora).
Message rows use theme colors and live display metrics; radio samples come from
the session snapshot, never from LVGL-side hardware access. Rendered history is
bounded to 24 entries and preserves the scroll position when new messages arrive.
The close hook and delete fallback release UI timers; leaving the screen does
not stop the mesh session. The keyboard accepts a retained draft and reflows in
landscape. Immediate queue failures retain that draft for retry.

Sub-GHz > Radio Config selects complete AM270/AM650/FM238/FM476 profiles, plus
AM812/FM952. Selecting a preset populates Mode, BW, Rate, Dev, LNA and DVGA.
The first settings item is a large inline frequency dial, with a central value,
MHz/AUTO caption, left/right arrows and a tick ruler. Preset, Mode, BW, Rate, Dev,
LNA and DVGA remain the existing compact rows beneath it. UP/DOWN moves between
fields; LEFT/RIGHT changes the selected dial immediately across 52 fixed carriers
and Hop bands. OK is inert on the dial; BACK returns to Sub-GHz with settings
retained. No picker page or confirmation step is involved. The dial is part of
the shared scroll container, so other fields remain reachable in both display
orientations. Theme colors come from `current_theme`; labels/ruler are owned by
the config screen, with no task, timer or radio ownership. The ruler is a discrete
selection indicator rather than an RSSI trace or linear frequency scale.
LEFT/RIGHT or OK edits bandwidth, rate, FSK deviation and gain limits individually;
Mode follows the preset and OOK deviation shows N/A. A `*` marks a customized
profile. `Reset preset` restores defaults while preserving frequency; selecting a
different preset reloads all modem fields. LNA/DVGA hints explain their AGC limits.
Settings update immediately in RAM; BACK returns without an extra Apply step,
and re-entry reflects current values. The screen uses the shared themed menu and
live display metrics. Read passes a copied configuration to the receiver, which
applies it after acquiring the radio; the screen performs no hardware access.
Frequency Analyzer has its own [discovery service](../frequency_analyzer/README.md),
with Auto, Radio Config carrier selection, preset groups/full search, threshold, RSSI history, pause and Read RAW handoff.
The Waterfall action opens a separate spectrum/history screen with center, span,
RX bandwidth, cursor, max hold and pause controls. Center is selected on entry and
uses the same Radio Config catalog, including jumps across RF gaps. Each complete
row measures 40 frequencies to reduce sweep work; a narrower span increases detail.
Its bounded PSRAM history is
freed by the screen close hook; theme changes rebuild it.
RAW opens only after analyzer cleanup and preserves Radio Config modulation at
the detected frequency. Read and Read RAW explicitly select their respective modes. File replay uses its saved preset and
custom values. Read's Send action, Saved's Send action and the Send picker open a
shared transmit detail screen without emitting a signal on entry. It displays the
filename, frequency, protocol, key or RAW pulse count and transmission status.
Pressing OK immediately starts PTT and repeats while held, with at most one burst
outstanding; no LONG_PRESS delay is required.
Decoded bursts include the checked file/default frame repeats; RAW plays once.
Releasing OK stops TX for both short taps and long holds. BACK stops and leaves the detail
screen (returns to the picker, or to Sub-GHz for direct entry). Native transmission
pulses the status LED using its configured info color and brightness.
`tools/highboy/tests/test_subghz_send_ui.py` exercises the complete screen with real
LVGL in both orientations and light/dark themes, including long names, unsupported
protocols and cleanup. `tools/subghz_api/test_subghz_api.py` checks tap/hold/release,
no queued backlog, worker errors and configured spectrum API with host fakes.
`tools/frequency_analyzer/test_ui.py` covers the analyzer in both orientations and
themes, pending starts, RAW handoff, errors and cleanup.
`tools/highboy/tests/test_subghz_presets_ui.py`, included in `test_ui.command`,
checks preset reload, independent overrides, reset, OOK/FSK behavior, wraparound,
frequency preservation and screen re-entry
with the real menu/LVGL at 240x320 and 320x240 using light/dark theme fixtures.
It does not validate RF reception on hardware.
Read now displays Listening → Receiving → Analyzing → result. Its themed graph is
measured RSSI history (64 samples at 50 ms), replacing the decorative sine/bit
animations. Frequency, live dBm, elapsed time, pulse count and RMT block count are
visible while collecting; hopping pauses on the captured frequency. The graph
uses remaining chrome/content height in portrait and landscape. Protocol/key
identification appears only after collection. The result retains Send, Save/name,
Capture again and Back; limits are explicitly titled “Capture limit reached”.
A failed write does not expose a false saved filename. The receiver owns all
sampling/collection/analysis and publishes snapshots; the screen only renders.
`test_subghz_read_ui.py` checks phase transitions, real RSSI projection bounds,
result navigation, startup errors and release in both orientations/themes.

Read, Send and saved-signal replay monitor worker results: “Signal sent” requires
RMT completion and confirmed radio IDLE, rather than an animation timer. Radio
failures stop the scan/spectrum animation and show an error with BACK available.
Their close hooks cancel owned operations and remove UI timers before navigation,
with idempotent deletion callbacks as a fallback;
Workers retain radio/buffer
ownership until hardware teardown succeeds, even after the screen closes.

## Handshake capture feedback

The storage card shows pending/peak queue depth, lost/failed frame counts, and
TX attempt/failure counts. The live card shows the scan status, remaining
listening time and the last AP attempt number; no SSID filter is applied. INFO
indicates driver acceptance of a TX call; warning indicates TX failure. Neither
means a client reconnected or a handshake was saved.
BACK displays “Finishing SD writes...” while the worker drains accepted frames,
then returns to the Wi-Fi menu; UI input/rendering remains asynchronous.

The Wi-Fi handshake screen uses an accent-bordered live-status card, paired RX
and saved-EAPOL counter cards, PAIR and PMKID badges, and a storage information
card. Only the eight recent per-AP capture entries remain a plain-text list.
Cards use content-driven heights and proportional widths. The panel reuses the
menu component track/thumb through `menu_component_attach_scroll()` and its
bounded `menu_component_scroll_content()` API. UP/DOWN stops at the actual content
limits; live updates clamp the offset after layout changes, and a panel with no
overflow hides the track/thumb. BACK exits. No decorative animation implies packet reception.
C5 EAPOL-pair (M1+M2, M2+M3 or M3+M4) and PMKID observations are distinct from successful SD writes. Colors use
the active theme, and the content stays inside header/footer bounds in portrait
and landscape. The worker publishes snapshots without accessing LVGL directly;
generation checks discard updates after navigation. See [Wi-Fi capture](../wifi/README.md#handshake-capture-and-lifecycle)
for persistence and purple INFO LED semantics. Hardware visual/LED acceptance is
still required; host tests cover persistence and worker cancellation.


### Classic read and reader-key extraction

Read Tag offers all blocks for Mini, 1K and 4K. Block navigation uses the actual
card geometry, including large 4K sectors. Progress reports actual sector,
block and key totals. Complete, partial and cancelled reads stay in RAM until an
explicit **Save card and dump**; this is also available through OK in the block
viewer. The name editor cancels without writing a dump. BACK from the viewer or
read error returns to the current card's options. BACK during reading cancels
safely before showing those options, retaining partial data. **View read blocks**
reopens the same result and block position; **Read blocks again** performs RF
acquisition. A new scan or leaving the session discards unsaved data.

Saving runs in a worker and reports failure without losing the RAM result;
repeated Save does not duplicate an already saved dump. Unread bytes remain
unknown in an explicitly saved Flipper profile. Bulk result buffers are static
PSRAM, not the worker stack. The screen stop hook requests cancellation and
waits for worker cleanup, including any pending storage write, before navigation.

Host coverage in `tools/highboy/tests/test_nfc_read_flow_ui.py` executes the
production state transitions and rendering with real LVGL, deferred worker
fixtures and mocked RF/storage. It checks explicit saves, cancellation, retry,
worker teardown and both display orientations; physical NFC/SD behavior still
requires device validation.

After identifying a Classic card, **Extract reader keys** displays capture and
pair counts. Remove the physical card and present High Boy to the reader more
than once. OK stops RF and begins offline MFKey recovery. BACK cancels capture
or recovery. The final screen distinguishes validated keys from keys actually
saved to the dictionary. RIGHT rereads the physical card, OK starts a new
capture. The display updates only when counts/state change during capture.
No card-only nested-recovery action is advertised: the old implementation was
invalid and is disabled. Full Flipper feature parity is not yet established.


The Classic block reader follow-up suppresses field-ring animation and limits
changed progress-label redraws to five per second. NFC and LCD share SPI; the
radio worker retains its independent cancellation callback, and BACK handling
continues on every UI refresh. Physical comparison is recorded with the NFC test
artifacts rather than inferred from the layout change.

The block result keeps block/key totals on a single line. Saved/not-saved status
is inside the block panel beside the sector keys, avoiding the previous wrapped
status overlapping the panel at 240 pixels wide. Success/warning colors use the
central semantic palette.

On-device result layouts were checked at 240x320 with Default (Mini and 4K)
and 320x240 with Cyber Blue (Mini), including the manufacturer block, totals,
saved-state row and footer. The original theme/orientation were restored.

## Battery, vibration and media presentation

Battery status remains visible with an unqualified fuel-gauge model. The main
reading then displays the filtered voltage estimate, marked with `~`, while
measured voltage, source, net current and blocked reason remain independent. The charge action only queues the guarded service request.
Portrait and landscape use content bounds; USB has its own header symbol.

Vibration is limited to accepted LEFT/RIGHT navigation in the main coverflow:
one 70 ms request after the animation guard, respecting enable and intensity.
The close hook cancels pending playback. Other screens and operation results do
not trigger the motor. Vibration settings retain enable and intensity only,
without a test pulse or polling timer. Result cues still use sound and the LED
notification worker. Shared board-service polling remains active for its other
controls. See the canonical AW8623 and LED contracts.

The MP3 player lays out the cover above controls in portrait and beside them in
landscape. It accepts embedded JPEG/PNG covers, stores bulk metadata and PCM in
PSRAM and freezes the path/offset passed to the audio worker. Shared flags are
atomic. Failed audio writes stop playback instead of automatically cycling into
repeated errors; the screen reports the failure. Missing/failed cover allocation
uses the placeholder. Library rows show MP3/WAV, not invented durations.

## Capture workflows

NFC Read uses the approach/return loop derived from the supplied video while the actual NFC manager
searches. The native GIF is 160x148, 12 fps, 48 palette entries, 171,553 bytes; only empty
source margins were cropped. The first 1.5 seconds play forward/backward in a
three-second loop; the success phase belongs to the actual card event. The outside background is transparent; closed dark
details in the device remain opaque. Full-canvas frames and disposal mode 2 clear
the previous silhouette without trails. ARGB8888 preserves alpha while drawing,
at its native 160x148 size (scaled down only if the body is smaller). The asset is loaded once off the LVGL task into
checked PSRAM. The flash-reading task uses an internal stack because flash I/O
disables the cache temporarily. The existing field illustration is a missing/failed-asset
fallback. All GIF timers stop on success, error and exit, before block transactions
on the shared display/NFC SPI bus. A real card-found event alone shows a green
identification check for 700 ms, then the card information for 1,400 ms and actions.
This confirms identification, not that every protected block has been read.
Retry/timeout never produces success. Opening NFC Read starts a new scan.
The GIF and its notice are added to the assets LittleFS partition through a guarded
update that verifies the installed local image and preserves every existing file;
an application-only flash cannot install this asset.

IR capture Save and Sub-GHz capture Save now open the shared filename editor,
with a suggested stem and an explicit extension hint. BACK cancels the name;
Sub-GHz's existing automatic capture file remains on SD until a successful rename.
Name conflicts are reported without replacing an existing destination. Missing
Sub-GHz capture files no longer produce a false Saved confirmation. RFID read and
manual-entry saves also request a name; this library still lasts only until reboot.
NFC identification uses the contextual name editor. Successful NFC/IR saves update
the result card's title to the chosen name. Saved NFC cards open explicit actions
without a timed transition; the emulation picker retains their identity and name.
Opening Emulate directly from the NFC menu also uses the refreshed library names.
Multiple saved aliases for one card remain named, selectable entries; the file
name stays underneath, while the card title and active emulation overlay retain
the selected saved name. Matching still requires the complete UID and protocol
family; an unlinked dump falls back to its file stem.
Deleting a saved NFC card also deletes its associated dumps and Classic companions
on SD, with confirmation, worker progress and checked completion. NFC Files uses
the same action-panel appearance for Open details / Rename / Save a copy / Delete
card + data. Rename retains file contents and synchronizes an unambiguous library
name. BACK during mutation waits for the result before leaving; close hooks release
the owned worker and timers. See the NFC document for SD/error and identity limits.
IR Browse Signals opens named captures directly, with protocol subtitles and
Send/Rename/Delete actions. The shared capture-result panel uses live screen
metrics, scrolls its actions when necessary in landscape, and scrolls long titles.
See [NFC naming and dump selection](../nfc/README.md) and
[IR captured signals](../ir/README.md#captured-signals-and-names) for persistence
and missing-data behavior. No folder chooser is introduced.

Sub-GHz Read/Read RAW results remain in PSRAM until Save is chosen and a name is
confirmed. Send can prepare an unsaved result directly in memory; Back/Discard
and Capture again release it without creating an SD file. The Radio Config dial
uses one full-height gradient in portrait and landscape. See the
[Sub-GHz receiver flow](../SubGhz/README.md#receiver-subghz_receiver).

Diagnostics uses two columns in landscape and a bounded body above the footer;
its timer now has a central close hook and a guarded DELETE fallback. Chip labels
the die temperature explicitly. Offline LoRa telemetry retains the scroll hint.

Run test_keyboard_ui.py, then test_ux_flows.py with the same host build directory:
the latter exercises naming callbacks, actual temporary-file
renames/collisions, and the Diagnostics layout with the pinned LVGL library in both
orientations. Diagnostics timer re-entry and stale-root deletion are covered.

Build configuration: the physical editor uses buttonmatrix + textarea. The unused
stock keyboard, calendar, window, dropdown, roller, spinbox and scale widgets are
disabled, as are the bundled LVGL
examples and demos. No first-party application or app-runtime export uses the
removed widgets/examples. Kconfig updates go through the pinned IDF configuration
server; flash partitions, radio configuration and charging limits are unchanged.

Run `test_home_ui.py` and `test_nfc_guidance_ui.py` with the same host
build directory to exercise Home layout/navigation/lifecycle, Settings as the
default shortcut, manual pins across pages, themes, GIF decoding and event-gated
success ordering. The host card event is a fixture, not a physical
RF reading. Device acceptance separately checks visible animation and physical
input navigation. Console `uistate` includes the top layer so modal cards and
status overlays are inspectable as well as the active root.


## Repeatable host regression checks

After resolving the pinned P4 dependencies with `bash tools/build.sh p4`, install
`tools/highboy/tests/requirements-ui.txt` and run
`bash tools/highboy/test_ui.command /tmp/highboy-ui-host`. This builds the native
LVGL fixture once, then checks the keyboard, capture naming and Diagnostics,
Home layout/navigation/lifecycle and transparent NFC guidance. The P4 CI build
runs the same sequence. Host card events and graphics are fixtures; physical RF
acceptance remains a separate device check.

`bash tools/highboy/test.command` also runs the prototype battery/LED, radio,
NFC, storage and screen-lifecycle regressions. `test_saved_capture_flows.py` checks
IR file round trips and failed writes, full NFC identity and alias selection, and
native LVGL saved-signal actions/layout in both orientations and light/dark themes.
These host tests do not open
serial devices or transmit over radios.
