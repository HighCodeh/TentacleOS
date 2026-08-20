# ui_manager

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

Enabling a radio for a whole area is done at the menu, not here: `menu_ui.c`'s
`ensure_radio_on()` powers Wi-Fi / BLE on when the user opens that area's menu.

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
[input-migration.md](input-migration.md) for the full pattern and gotchas. Every
screen already uses this model; the lone exception is `games/octopet_ui.c`,
which keeps a poll timer on purpose for continuous held-direction movement.

## Screen power policy (auto-dim / sleep)

`components/ui/components/power_policy` owns the always-on display power
behaviour in a single `lv_timer`, started once from `ui_init` under the LVGL
lock. It reads `input_last_activity_ms()` (from `input_manager`) and the
`screen` config (`g_config_screen.auto_lock_seconds`, `auto_dim`):

- After `auto_lock_seconds` of no input it **fades** the backlight (a ramp, not
  a step) and, when faded to zero, calls `lcd_display_sleep(true)` to cut the
  panel. When `auto_dim` is set it first dims to a low level a few seconds
  before sleeping.
- The user's chosen brightness is captured before dimming and restored on the
  next input, so auto-dim never overwrites it. Transient changes use
  `lcd_apply_brightness` (no persist); see [st7789](../st7789/README.md).
- Any input wakes the screen. `power_policy_is_asleep()` gates the central input
  router so the wake press only wakes the display instead of also acting on the
  underlying screen.

The display settings screen writes `auto_lock_seconds` / `auto_dim` / brightness
through `tos_config`, which is the sole writer of the `screen` config file.

## Execution flow summary
1. User selects **Bluetooth** in the main menu.
2. The menu resolves the item's `target` (`SCREEN_BLE_MENU`), calls
   `ensure_radio_on(target)` (powers BLE on for the area), then
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
