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

# LED status driver

P4 drives the LP5816 at I2C0 address 0x2C. OUT0/1/2 are red/green/blue,
OUT3 stays disabled, and the 25.5 mA dot-current ceiling is unchanged.
[TI's register interface](https://www.ti.com/lit/ds/symlink/lp5816.pdf) supports
one auto-increment transaction for the three RGB PWM registers.

## P4 ownership and feedback

One checked 3072-byte task (`SYS_PRIO_BACKGROUND`, `SYS_CORE_RADIO`) owns runtime
I2C writes. Callers only publish state under a critical section; ISR requests
are rejected. Initialization checks every hardware write and task creation.
I2C failures are retried at the worker cadence, with logs only when the
error changes. Unchanged colors do not cause bus writes. Idle cadence is 100 ms; while an
activity source is present it is 5 ms, resolving the short active pulse.

`led_signal_info/warning/error()` displays a 350 ms notification using the
configured colors and brightness. Error outranks warning, which outranks info.
Repeated equal/lower-priority events cannot stretch a pulse indefinitely.
After the pulse, the previous manual preview or power indication resumes.
`led_set_signal_config()` clamps brightness to 0–100, including full mute.

`led_set_power_state()` accepts OFF, CHARGING, FULL, BLOCKED or LOW. The battery
service owns these measured states; USB presence alone is not charging.
Normal charge indication follows the backlight state published through
`led_set_display_active()`: no recurring charge LED while the screen is lit;
with the screen dark, a red pulse lasts 200 ms every 10 seconds, starting when
charging or screen-off begins. Dimming above zero still counts as a lit screen.
Repeated state publications do not restart the period. FULL opens one green
indication window of at most one second, including with the screen lit, then
extinguishes; repeated FULL samples and screen toggles do not replay it.
Notifications/previews/activities may preempt this window; it is not queued.
Charge/full brightness is capped at 10% and also respects a lower configured
brightness or mute. Blocked/low retains the configured warning/error color for
300 ms every 2.4 seconds. These alerts are distinct from normal charge status.

ST7789 publishes the backlight state only after both PWM calls succeed; a failed
update retains the previous state. The LED driver defaults to screen-active
until that first publication. State changes wake the existing LED worker;
there are no new tasks, timers or SPI requests. All RGB I2C writes remain owned
by that worker.

`led_set_color(r,g,b)` holds a manual preview. `led_clear()` releases it and
restores the power indication. Semantic notifications can temporarily override
that preview. Blocking `led_blink*()` compatibility functions remain task-only.
Settings can still select notification colors and brightness.

UI read/write/emulation/SD feedback requests an info flash on its existing
rate-limited worker. Routine navigation does not blink the LED.

`led_set_activity(LED_ACTIVITY_NFC, true)` adds an info-color pulse for 10 ms
every 100 ms (10 Hz) while the central NFC listener is RUNNING. Cleanup releases
only that source bit. The activity owns its entire cadence, including the dark
gaps, so background battery colors cannot appear between application pulses.
Notifications and manual previews take precedence. `led status` reads the actual PWM registers,
configured brightness, power state and activity mask (not optical brightness).
The 10/100 ms cadence follows the fast blink preset in the official
[Flipper notification definitions](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/notification/notification_messages.c).
The P4 worker is software scheduled, so this is not an optical timing guarantee.

## Validation

`python3 tools/highboy/tests/test_power_feedback.py` executes the production
LED driver with I2C/task fakes: init failures, task failure, priorities, bounded
pulses, screen-on/off charge suppression, the 200 ms/10 s cadence, FULL expiry,
activity gap ownership, brightness clamps/mute and ISR rejection. The same host
harness executes `lcd_apply_brightness()` with PWM success/failure fakes to verify
state publication. It also runs the production display inactivity functions
across dim/sleep and repeated sleeping ticks for two charge periods, with and
without auto-dim, then verifies input wake suppresses charging pulses again.
Optical color,
current and visible intensity still require observation on the board.

# C5

RGB status LED backed by an addressable RMT LED strip (single pixel), driven
through the `led_strip` component. This is not the LP5816 used on P4, and there is
no I2C, signal API, or dedicated off task.

## Overview

- **Location:** `components/Drivers/led/`
- **Header:** `include/led_control.h`
- **Interface:** RMT (single-pixel addressable LED via the `led_strip` component)
- **Data pin:** `GPIO_LED_RGB_PIN` (GPIO 27, from `pin_def.h`)
- **Dependencies:** `led_strip`, `pin_def`, `freertos`

## Configuration / Tunables

- `RMT_RESOLUTION_HZ` = `10 MHz`: RMT channel resolution.
- Strip: `max_leds = 1`, `RMT_CLK_SRC_DEFAULT`, DMA disabled.
- Blink colors/durations:

| Helper | RGB | Duration |
| :--- | :--- | :--- |
| `led_blink_red` | 255,0,0 | 500 ms |
| `led_blink_green` | 0,150,0 | 220 ms |
| `led_blink_blue` | 0,0,255 | 500 ms |
| `led_blink_purple` | 200,0,220 | 500 ms |

## API Reference

### Initialization

#### `led_rgb_init`
```c
void led_rgb_init(void);
```
Creates the single-pixel RMT LED strip device on `GPIO_LED_RGB_PIN` and clears it.
Uses `ESP_ERROR_CHECK`, so a failure aborts.

### Fixed-color Blink Helpers

```c
void led_blink_red(void);
void led_blink_green(void);
void led_blink_blue(void);
void led_blink_purple(void);
```
Each sets the pixel to its color, refreshes, blocks for the color's duration, then
clears and refreshes. No-op if the strip is not initialized.
