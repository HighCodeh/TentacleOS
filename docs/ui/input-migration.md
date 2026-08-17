# Screen input migration guide

How to convert one screen from the old per-screen polling `lv_timer` to the
central event-driven input. Background: [input_manager](../input_manager/README.md),
[ui: event-driven handlers](README.md#input-event-driven-handlers).

**Status:** 7 screens migrated (see [Reference examples](#reference-examples));
~91 still use `nav_timer_cb`. Find the remaining ones with:

```sh
grep -rln "nav_timer_cb\|nav_cb\b" firmware_p4/components/Applications/ui/screens --include='*.c'
```

Nothing is broken in the meantime: unmigrated screens keep working through the
`buttons_gpio` shim. Migrate opportunistically, build after each batch.

## What you are replacing

Old pattern (every screen has its own copy):

```c
#include "buttons_gpio.h"
#define NAV_TIMER_MS 50
static lv_timer_t *s_nav_timer = NULL;
static bool s_btn_up_last = false; /* ... one per button ... */

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) { lv_timer_delete(t); s_nav_timer = NULL; return; }
  if (ui_input_is_locked()) return;
  bool up = ui_btn_up(); bool down = ui_btn_down(); /* ...read all buttons... */
  if (down && !s_btn_down_last) menu_component_next(&s_menu);   // edge detection
  /* ... */
  s_btn_up_last = up; /* ...store last state... */
}
/* in the open function: */
if (s_nav_timer == NULL) s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
```

New pattern: a handler that receives one debounced event at a time.

```c
static void my_screen_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav   = press || (ev->action == INPUT_ACTION_REPEAT); // held auto-scroll
  switch (ev->button) {
    case INPUT_BTN_DOWN: if (nav) menu_component_next(&s_menu); break;
    case INPUT_BTN_UP:   if (nav) menu_component_prev(&s_menu); break;
    case INPUT_BTN_OK:   if (press) { /* select */ } break;
    case INPUT_BTN_BACK: if (press) ui_switch_screen(SCREEN_MENU); break;
    default: break;
  }
}
/* in the open function, instead of the lv_timer_create: */
ui_input_set_screen_handler(my_screen_input, NULL);
```

## The recipe

1. **Delete** the polling scaffolding: the `#include "buttons_gpio.h"`, the
   `#define NAV_TIMER*`, the `static lv_timer_t *s_nav_timer`, and all the
   `static bool s_*_last` edge statics. Keep `s_screen` and `s_menu`.
2. **Rewrite** `nav_timer_cb`'s body as the handler above:
   - `if (X && !s_X_last)` (a fresh press) becomes `case INPUT_BTN_X: if (press)`.
   - Navigation (UP/DOWN, or LEFT/RIGHT carousels) should use `nav` (PRESS +
     REPEAT) so holding auto-scrolls. Actions (OK/BACK/select) use `press` only.
   - Drop the `lv_screen_active()` guard, the `ui_input_is_locked()` guard, and
     any `msgbox_is_open()` / `keyboard_is_open()` guards - the central pump
     already handles all of them.
3. **Register** it: replace the `lv_timer_create(nav_timer_cb, ...)` line (and
   the `if (s_nav_timer == NULL)` around it) with
   `ui_input_set_screen_handler(my_screen_input, NULL);`.
4. **Build** and fix (see gotchas). Do not add a handler-clear on close; the UI
   manager clears it on the next screen switch.

## Gotchas (these bit me - check each)

- **Definition order / forward declaration.** If the handler is defined *after*
  the open function (common when `nav_timer_cb` was forward-declared and its body
  sat at the bottom), the open function can't see it: add a forward declaration
  near the top, e.g. `static void my_screen_input(const input_event_t *ev, void *ctx);`.
- **Handler references data defined lower in the file.** If your handler uses a
  `MENU_ITEMS[]` array that is declared *below* where you put the handler, you'll
  get "undeclared identifier". Put the handler *after* that array (where the old
  `nav_timer_cb` body was), not up where the statics were.
- **State reset in the open function.** Some screens reset the edge statics on
  open (`s_up_last = s_down_last = ... = false;`). Delete that line too - the
  statics are gone.
- **Screens that need a continuous held state** (games: hold a direction to
  move) should read `input_is_down(INPUT_BTN_X)` from their own render/tick
  timer instead of edge events, or act on `INPUT_ACTION_REPEAT`. Do not force
  these into a pure press-handler.
- **Multi-view screens** (a menu plus sub-screens driven by `switch (s_view)`)
  keep the same structure: the handler switches on `s_view` first, then on
  `ev->button`. Convert each `if (X && !s_X_last)` branch in place.
- **`TAG` may become unused** once the polling code is gone. The coding standard
  keeps `TAG` in every `.c`; the unused-variable warning is not an error.

## Reference examples

Copy the closest match:

| Screen | Pattern it shows |
|--------|------------------|
| `nfc/nfc_menu_ui.c` | Plainest menu: UP/DOWN nav, OK/RIGHT enter, BACK/LEFT out |
| `wifi/wifi_ui.c` | Same, but handler sits after the `MENU_ITEMS[]` array (ordering) |
| `connect_bluetooth/connect_bt_ui.c` | Any of OK/RIGHT/BACK/LEFT does one action |
| `infrared/ir_menu_ui.c` | Forward-declared handler (defined after `open`) |
| `dev/dev_menu_ui.c` | Forward decl **and** an edge-state reset removed from `open` |
| `bluetooth/ui_ble_menu.c` | Forward decl + `MENU_ITEMS` target dispatch |
| `games/games_menu_ui.c` | Carousel: LEFT/UP and RIGHT/DOWN cycle an index with REPEAT |

## Remaining work (~91 screens)

By area (run the grep above for the live list): bluetooth 15, wifi 14, nfc 13,
settings 11, lora 8, infrared 7, subghz 5, audio 4, dev 3, games 2, badusb 2,
and one each in theme, rfid, haptic, gpio, files, connect_wifi,
connection_settings. The simple area menus (settings, submenus) are quickest;
the games and multi-view NFC/RFID screens need the continuous-state / multi-view
handling above.

## Verify

```sh
cd firmware_p4 && idf.py build
```

A migrated screen should navigate exactly as before, plus hold-to-scroll on
UP/DOWN, and it no longer runs a per-screen timer.
