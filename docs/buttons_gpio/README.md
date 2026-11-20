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

# P4

> **Now a compatibility shim.** As of the input rework, `buttons_gpio` no longer
> touches GPIO directly - it forwards to [`input_manager`](../input_manager/README.md),
> which owns sampling, debounce, long-press/repeat, the event queue, activity
> tracking and the wake source. These functions still work exactly as before, so
> existing call sites need no changes. New code should consume `input_manager`
> events instead.

This component exposes the physical input buttons of the Highboy device: poll
button states with both "is pressed" (continuous) and "was pressed" (one-shot)
logic.

## Overview

- **Location:** `components/Drivers/buttons_gpio/`
- **Header:** `include/buttons_gpio.h`
- **Backed by:** [`input_manager`](../input_manager/README.md)

## Configuration

- **Input Mode:** `GPIO_MODE_INPUT` with internal Pull-Up enabled (in `input_manager`).
- **Active Level:** Low (`0`). Buttons connect to ground when pressed.
- **Debounce:** Time-based (~20 ms), in `input_manager`. `buttons_task()` is now a
  no-op: sampling runs in the `input_manager` timer.

## Key Mapping

| Button | Function |
| :--- | :--- |
| **BTN_UP** | Up Navigation |
| **BTN_DOWN** | Down Navigation |
| **BTN_LEFT** | Left / Decrease |
| **BTN_RIGHT** | Right / Increase |
| **BTN_OK** | Enter / Select |
| **BTN_BACK** | Back / Escape |

## API Reference

### Initialization

#### `buttons_init`
```c
void buttons_init(void);
```
Configures the GPIO pins defined in `pin_def.h` as inputs with pull-ups. Initializes the state of all buttons.

### State Checking (One-shot)
These functions return `true` **only once** per press. They rely on the `buttons_task` or interrupt logic (conceptually) setting a flag, and these functions reading/clearing it atomically.

- `bool up_button_pressed(void)`
- `bool down_button_pressed(void)`
- `bool left_button_pressed(void)`
- `bool right_button_pressed(void)`
- `bool ok_button_pressed(void)`
- `bool back_button_pressed(void)`

### State Checking (Continuous)
These functions return the **current raw state** of the button. Returns `true` as long as the button is held down.

- `bool up_button_is_down(void)`
- `bool down_button_is_down(void)`
- `bool left_button_is_down(void)`
- `bool right_button_is_down(void)`
- `bool ok_button_is_down(void)`
- `bool back_button_is_down(void)`

### Tasks

#### `buttons_task`
```c
void buttons_task(void);
```
Updates the internal state of the buttons. This should be called periodically (e.g., in a FreeRTOS task or timer callback) to detect state changes (edges) and set the `pressed_flag`.

---

# C5

V2.03 buttons are all on P4. The old C5 GPIO button implementation is excluded
from the build and must not be initialized.

## V2.03 mapping and gestures

| Schematic | Button | P4 GPIO |
| --- | --- | --- |
| B1 | BACK | 6 |
| B2 | OK | 32 |
| B3 | RIGHT | 13 |
| B4 | DOWN | 7 |
| B5 | LEFT / boot strap | 35 |
| B6 | UP | 3 |

All six inputs are active-low, with pull-ups. Input initialization seeds held
buttons without generating a synthetic PRESS, preserving boot-time recovery.
OK+BACK at boot enters firmware recovery. Holding BACK alone for five seconds
opens software shutdown confirmation; OK or DOWN suppresses that gesture.
The old OK+LEFT shutdown shortcut is removed to avoid conflicting UI chords.

OK+DOWN (B2+B4) is wired to TPS3421EGDRYR independently of the application.
Sheet 6 ties TS to ground; the nominal delay is 7.5 s, followed by a 400 ms reset
pulse per the [TI datasheet](https://www.ti.com/lit/ds/symlink/tps3421.pdf).
This is an electrical design expectation, not a measurement of the assembled
board. BACK also connects to BQ25896 QON; LEFT is the ROM boot strap. Firmware
cannot override those hardware effects. Check actual timing/mounting, held-key
boot and shutdown on battery and VBUS before accepting the migration.

`tools/board_control/test_consumers.py` compiles the real input manager and power
gesture function: six-button mapping/debounce/release, wake mask, boot-held
recovery keys, BACK shutdown and suppression for hardware/recovery chords.
