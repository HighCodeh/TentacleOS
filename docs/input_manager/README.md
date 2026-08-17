# P4

Central button input for the P4. One periodic sampler debounces all six buttons,
drives a per-button state machine (press / release / long-press / auto-repeat),
pushes events onto a queue, tracks the last interaction for the power policy, and
registers the button GPIOs as a wake source.

## Overview

- **Location:** `components/Drivers/input_manager/`
- **Header:** `include/input_manager.h`
- **Dependencies:** `driver/gpio`, `esp_timer`, `esp_sleep`, `pin_def.h`

It replaces the pattern where ~110 screens each ran their own `lv_timer` polling
raw GPIO with per-screen edge detection. `buttons_gpio` is now a thin shim over
this module (see [buttons_gpio](../buttons_gpio/README.md)), so existing call
sites keep working unchanged while gaining debounce, activity tracking and wake.

## Why it exists

- **Single "user interacted" point.** `input_last_activity_ms()` gives the power
  policy one timestamp to watch, instead of editing every screen.
- **Long-press / repeat.** Enables hold-to-power-off and boot key-combos, and
  gives menus auto-repeat, without per-screen timers.
- **Time-based debounce.** Consistent regardless of any screen's poll period.
- **Wake source.** The button GPIOs are registered so light sleep can be woken by
  a button.
- **Fewer always-on timers.** Migrated screens drop their polling `lv_timer`.

## Sampling and timing

A periodic `esp_timer` ticks every `SAMPLE_PERIOD_MS` (5 ms), independent of the
LVGL render loop, so input is sampled reliably even when rendering is busy.

| Constant | Default | Meaning |
|----------|---------|---------|
| `SAMPLE_PERIOD_MS` | 5 | Sampler tick |
| `DEBOUNCE_SAMPLES` | 4 | Stable ticks to accept a level (~20 ms) |
| `LONG_PRESS_MS` | 800 | Held this long emits `LONG_PRESS` once |
| `REPEAT_DELAY_MS` | 400 | First `REPEAT` after a press |
| `REPEAT_PERIOD_MS` | 120 | Subsequent `REPEAT` interval |
| `EVENT_QUEUE_LEN` | 16 | Event queue depth |

A button held at boot is seeded as "pressed at boot" (no phantom `PRESS`), so a
boot key-combo times its long-press from boot rather than firing instantly.

## Events

```c
typedef struct {
  input_button_t button;    // INPUT_BTN_UP..INPUT_BTN_BACK
  input_action_t action;    // PRESS | RELEASE | LONG_PRESS | REPEAT
  uint32_t timestamp_ms;    // ms since boot
} input_event_t;
```

`PRESS`/`RELEASE` are debounced edges; `LONG_PRESS` fires once at the threshold;
`REPEAT` auto-repeats while held. A held button therefore emits `PRESS`, then
`REPEAT`s, and `LONG_PRESS` once at 800 ms - consumers use whichever they need.

## API

```c
esp_err_t input_manager_init(void);                         // sampler + queue + wake source
bool      input_get_event(input_event_t *out, uint32_t timeout_ms);  // consume events
bool      input_is_down(input_button_t button);             // debounced held state
bool      input_consume_press(input_button_t button);       // latched edge (backs the shim)
uint32_t  input_last_activity_ms(void);                     // timestamp of last activity
void      input_sim_press(input_button_t button, uint32_t ms);  // headless / console `key`
esp_err_t input_configure_wake_source(void);                // re-arm GPIO light-sleep wake
```

Idle time for the power policy is `now_ms - input_last_activity_ms()`.

## Event-driven screens (the router)

Screens do not call `input_get_event()` themselves. The UI manager runs **one**
pump (`ui_input_pump`, a single `lv_timer`) that drains the queue and dispatches
each event to the active screen's handler. This replaces the ~110 per-screen
polling `lv_timer`s with one shared timer.

- A screen registers its handler in its open function:
  `ui_input_set_screen_handler(my_input, NULL)` (see `ui_manager.h`).
- The handler is cleared automatically on the next screen switch
  (`clear_current_screen`), so screens never leak handlers or fight for input.
- The pump swallows input while `ui_input_is_locked() || msgbox_is_open() ||
  keyboard_is_open()`, so the old per-screen guards are gone and screens freeze
  correctly under overlays. The dropdown refreshes the input lock while open, so
  it is covered too.
- Migrated and not-yet-migrated screens coexist: a screen that has not been
  migrated simply leaves the handler NULL and keeps its own polling timer.

Handler pattern (from `nfc_menu_ui.c`):

```c
static void nfc_menu_input(const input_event_t *ev, void *ctx) {
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav   = press || (ev->action == INPUT_ACTION_REPEAT); // held auto-scroll
  switch (ev->button) {
    case INPUT_BTN_BACK:  case INPUT_BTN_LEFT:  if (press) ui_switch_screen(SCREEN_MENU); break;
    case INPUT_BTN_OK:    case INPUT_BTN_RIGHT: if (press) { /* open selected */ } break;
    case INPUT_BTN_DOWN:  if (nav) menu_component_next(&s_menu); break;
    case INPUT_BTN_UP:    if (nav) menu_component_prev(&s_menu); break;
    default: break;
  }
}
```

The `PRESS` event *is* the debounced edge, so the per-screen `s_*_last` edge
statics disappear. Use `INPUT_ACTION_REPEAT` for held auto-repeat (menu scroll),
and `input_is_down()` for screens that need a continuous held state (e.g. games).

**Migrating the existing screens:** 7 are done; ~91 still poll. The step-by-step
recipe, gotchas and reference examples are in
[ui/input-migration.md](../ui/input-migration.md).
