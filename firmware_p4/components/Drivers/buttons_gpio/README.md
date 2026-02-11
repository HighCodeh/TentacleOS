# GPIO Buttons Driver

This component handles the physical input buttons of the Highboy device. It provides functions to initialize GPIOs and poll button states, supporting both "is pressed" (continuous) and "was pressed" (one-shot/flag) logic.

## Overview

- **Location:** `components/Drivers/buttons_gpio/`
- **Header:** `include/buttons_gpio.h`
- **Dependencies:** `driver/gpio`, `pin_def.h`

## Configuration

- **Input Mode:** `GPIO_MODE_INPUT` with internal Pull-Up enabled.
- **Active Level:** Low (`0`). Buttons connect to ground when pressed.
- **Debounce/Polling:** Handled via `buttons_task` or direct atomic flag checks.

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
