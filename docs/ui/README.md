# ui_manager
step-by-step process for adding a new screen (feature) to the HighBoy system using the ui_manager architecture.

**Example** used: We'll create a fictional **Bluetooth (BLE)** screen.

### 1. Register the screen in the UI ui_manager
The `ui_manager` needs to know about the new screen to handle navigation.

**File:** `ui/ui_manager.h`
1. Add a new identifier to the `enum`:
```c
typedef enum {
    SCREEN_NONE,
    SCREEN_HOME,
    SCREEN_MENU,
    SCREEN_WIFI_MENU,
    // ...
    SCREEN_BLE_MENU, // <--- NEW ID ADDED
} screen_id_t;
```

### 2. Configure routing and Power Management
Define how the ui_manager should open the screen and handle any required hardware power states.

**File:** `ui/ui_manager.c`
1. Include de header for the new screen (created in Step 3):
```c
#include "screens/bluetooth/ui_ble_menu.h"
```

2. (Optional) Power Management: If the screen uses a radio (Wi-Fi, BLE, RF), add logic to automatically enable/disable the hardware.
```c
static bool is_ble_screen(screen_id_t screen) {
    switch (screen) {
        case SCREEN_BLE_MENU:
        case SCREEN_BLE_SCAN: // Future sub-screens
            return true;
        default:
            return false;
    }
}
```

Update `ui_switch_screen` to call `ble_init()` / `ble_deinit()` based on this flag (similar to how Wi-Fi is handled).

3. Add the case to the main switch statement:
```c
void ui_switch_screen(screen_id_t new_screen) {
    if (ui_acquire()) {
        // ... init/deinit logic ...
        clear_current_screen();

        switch (new_screen) {
            // ... other cases ...

            case SCREEN_BLE_MENU: // <--- NEW ROUTE
                ui_ble_menu_open();
                break;
        }
        // ...
    }
}
```

### 3. Create the New Screen UI 
Create the folder and files for the new feature: `ui/screens/bluetooth/`

**Header File:** `ui_ble_menu.h`
```c
#ifndef UI_BLE_MENU_H
#define UI_BLE_MENU_H
#include "lvgl.h"
void ui_ble_menu_open(void); // Public function
#endif
```

**Source File:** `ui_ble_menu.c`
Standard template from any Highboy screen:

```c
#include "ui_ble_menu.h"
#include "ui_manager.h"
#include "lv_port_indev.h" // Access to main_group
#include "esp_log.h"

static const char *TAG = "UI_BLE";
static lv_obj_t * screen_ble = NULL;

// 1. Event Callback (Navigation)
static void ble_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        // BACK BUTTON (ESC/LEFT)
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            ESP_LOGI(TAG, "Returning to Main Menu");
            // Destroy current screen and open Menu
            ui_switch_screen(SCREEN_MENU);
        }
    }
}

// 2. Screen Build Function
void ui_ble_menu_open(void) {
    // Safety cleanup
    if (screen_ble) {
        lv_obj_del(screen_ble);
        screen_ble = NULL;
    }

    // A. Create Base Screen
    screen_ble = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_ble, lv_color_black(), 0);

    // B. Add Content (e.g., Title)
    lv_obj_t * label = lv_label_create(screen_ble);
    lv_label_set_text(label, "Bluetooth Menu");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    // C. Setup Navigation
    lv_obj_add_event_cb(screen_ble, ble_event_cb, LV_EVENT_KEY, NULL);

    // Add to Input Group (Essential!)
    if (main_group) {
        lv_group_add_obj(main_group, screen_ble);
        lv_group_focus_obj(screen_ble);
    }

    // D. Load Screen
    lv_screen_load(screen_ble);
}
```

### 4. Link from the main Menu
Add a button/entru in the main menu to access the new screen

**File:** `ui/screens/menu/ui_menu.c`
1. In the `menu_event_cb` callback, locate the corresponding item ID case and add/uncomment the call:
```c
case MENU_ID_BLUETOOTH:
    ui_switch_screen(SCREEN_BLE_MENU); // <--- Routes to the new screen
    break;
```
(Note: If the MENU_ID_BLUETOOTH entry doesn't exist yet in menu_item_id_t, create it.)

### 5. Update Build System (CMake)
Commom error: forgettint to register the new source files.

**File:** `CMakeLists.txt` (UI component)
1. Add the new sources files and include directory:
```cmake
file(GLOB_RECURSE HOME_UI_SRCS "ui/screens/home/*.c")
file(GLOB_RECURSE MENU_UI_SRCS "ui/screens/menu/*.c")
file(GLOB_RECURSE WIFI_UI_SRCS "ui/screens/wifi/*.c")
file(GLOB_RECURSE BLE_UI_SRCS  "ui/screens/ble/*.c") # <---- Add srcs here

idf_component_register(SRCS 
  "ui/ui_manager.c"
  ${HOME_UI_SRCS}
  ${MENU_UI_SRCS}
  ${WIFI_UI_SRCS}
  ${BLE_UI_SRCS} # <----- and call it here

  INCLUDE_DIRS 
  "ui/include"
  "ui/screens/home/include"
  "ui/screens/menu/include"
  "ui/screens/wifi/include"
  "ui/screens/ble/include" # <----- dont forget include files
)
```
2. Recommended: Run `idf.py reconfigure` in the terminal after saving

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

See `nfc_menu_ui.c` for the reference migration, and
[input-migration.md](input-migration.md) for the step-by-step guide to convert
the remaining ~91 screens off their polling timers (with the gotchas).

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

## Execution Flow Sumamary
1. User selects **Bluetooth** from the Main Menu.
2. Menu callback calls `ui_switch_screen(SCREEN_BLE_MENU)`.
3. `ui_manager`:
  - Handles hardware power (enables BLE if needed).
  - Clears previous screen.
  - Calls `ui_ble_menu_open()`.
4. `ui_ble_menu_open`:
  - Creates visual objects.
  - Adds objects to `main_group`.
  - Loads the screen.

**Done! The new screen is fully integrated, safe and navigable.**
