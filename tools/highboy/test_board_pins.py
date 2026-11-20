#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Execute production boot GPIO code with GPIO failure injection."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NOTICE = (ROOT / "firmware_c5/components/Drivers/pins/board_pins.c").read_text().split("#include")[0]


def main():
    with tempfile.TemporaryDirectory(prefix="highboy-v203-") as temporary:
        tmp = Path(temporary)
        (tmp / "driver").mkdir()
        (tmp / "freertos").mkdir()
        fixtures = {
            "freertos/FreeRTOS.h": "#define pdMS_TO_TICKS(ms) (ms)\n",
            "freertos/task.h": "void vTaskDelay(int ticks);\n",
            "esp_err.h": "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL 1\n#define ESP_ERR_INVALID_STATE 2\n",
            "esp_log.h": '#define ESP_LOGE(tag, ...) ((void)(tag))\n#define ESP_LOGI(tag, ...) ((void)(tag))\n',
            "driver/gpio.h": '''#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 2
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 2
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int pin, void (*fn)(void *), void *arg);
esp_err_t gpio_isr_handler_remove(int pin);
esp_err_t gpio_set_intr_type(int pin, int type);
esp_err_t gpio_intr_enable(int pin);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
esp_err_t gpio_config(const gpio_config_t *config);
''',
        }
        for name, content in fixtures.items():
            (tmp / name).write_text(NOTICE + content)
        for target, pins in (("p4", [0, 1, 19, 33]), ("c5", [0, 5, 4, 6])):
            source = ROOT / f"firmware_{target}/components/Drivers/pins"
            harness = NOTICE + '#include <assert.h>\n#include "board_pins.c"\n'
            if target == "p4":
                harness += '_Static_assert(GPIO_NFC_CS_PIN == 49, "NFC CS");\n'
                harness += '_Static_assert(GPIO_BTN_OK_PIN == 32, "B2/OK");\n'
                harness += '_Static_assert(GPIO_BTN_DOWN_PIN == 7 && GPIO_BTN_BACK_PIN == 6 && GPIO_BTN_LEFT_PIN == 35 && GPIO_BTN_RIGHT_PIN == 13 && GPIO_BTN_UP_PIN == 3, "V2.03 buttons");\n'
                harness += '#if defined(GPIO_IR_TX_PIN) || defined(GPIO_AUDIO_EN_PIN)\n#error P4 must not own IR or amplifier control\n#endif\n'
            else:
                harness += '_Static_assert(GPIO_IR_RX_PIN == 10 && GPIO_HAPTIC_INT_PIN == 1, "C5 inputs");\n'
                harness += '#ifdef GPIO_BRIDGE_IRQ_PIN\n#error V2.03 has no IRQ trace\n#endif\n'
            harness += "static const int expected[] = {" + ",".join(map(str, pins)) + "};\n"
            harness += '''
static int calls, fail_at, is_control, control_pin, control_level;
static int reset_test, reset_calls, reset_fail, reset_delay;
static int download_test, download_calls, download_fail, boot_level, reset_level;
void vTaskDelay(int ticks) { reset_delay += ticks; }
int gpio_get_level(gpio_num_t pin) { (void)pin; return 1; }
static esp_err_t step(void) { return calls++ == fail_at ? ESP_FAIL : ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
  if (download_test) {
    assert(pin == 0 || pin == 1);
    if (download_calls++ == download_fail) return ESP_FAIL;
    if (pin == 0) {
      if (level == 1) assert(boot_level == 1 && reset_delay >= 10);
      if (level == 0 && reset_level == 1) assert(reset_delay >= 30);
      reset_level = level;
    } else boot_level = level;
    return ESP_OK;
  }
  if (reset_test) {
    const int pins[] = {1, 0, 0};
    const int levels[] = {0, 1, 0};
    assert(reset_calls < 3 && pin == pins[reset_calls] && (int)level == levels[reset_calls]);
    return reset_calls++ == reset_fail ? ESP_FAIL : ESP_OK;
  }
  if (is_control) { control_pin = pin; control_level = level; return ESP_OK; }
  assert(calls % 2 == 0);
  assert(pin == expected[calls / 2]);
  assert(level == 0);
  return step();
}
esp_err_t gpio_config(const gpio_config_t *config) {
#ifdef GPIO_HAPTIC_INT_PIN
  if (calls == 2 * (int)(sizeof(expected) / sizeof(expected[0]))) {
    assert(config->pin_bit_mask == (UINT64_C(1) << GPIO_HAPTIC_INT_PIN));
    assert(config->mode == GPIO_MODE_INPUT);
    assert(config->pull_up_en == GPIO_PULLUP_ENABLE);
    return step();
  }
#endif
  assert(calls % 2 == 1);
  assert(config->pin_bit_mask == (UINT64_C(1) << expected[calls / 2]));
  assert(config->mode == GPIO_MODE_OUTPUT);
  assert(config->pull_up_en == 0 && config->pull_down_en == 0 && config->intr_type == 0);
  return step();
}
#ifdef GPIO_HAPTIC_INT_PIN
static void (*irq_handler)(void *);
esp_err_t gpio_install_isr_service(int flags) { assert(flags == 0); return step(); }
esp_err_t gpio_isr_handler_add(int pin, void (*fn)(void *), void *arg) {
  assert(pin == 1 && arg == NULL); irq_handler = fn; return step();
}
esp_err_t gpio_isr_handler_remove(int pin) { assert(pin == 1); return ESP_OK; }
esp_err_t gpio_set_intr_type(int pin, int type) { assert(pin == 1 && type == GPIO_INTR_NEGEDGE); return step(); }
esp_err_t gpio_intr_enable(int pin) { assert(pin == 1); return step(); }
#endif
int main(void) {
  int total = 2 * sizeof(expected) / sizeof(expected[0]);
#ifdef GPIO_HAPTIC_INT_PIN
  total += 5;
#endif
  fail_at = -1;
  assert(board_pins_init() == ESP_OK);
  assert(calls == total);
  for (int i = 0; i < total; i++) {
    calls = 0; fail_at = i;
    assert(board_pins_init() == ESP_FAIL);
    assert(calls == i + 1);
  }
#ifdef GPIO_HAPTIC_INT_PIN
  is_control = 1;
  assert(board_pins_set_amplifier(true) == ESP_OK && control_pin == 0 && control_level == 1);
  assert(board_pins_set_amplifier(false) == ESP_OK && control_pin == 0 && control_level == 0);
  assert(board_pins_set_haptic_release(true) == ESP_OK && control_pin == 5 && control_level == 1);
  assert(board_pins_set_haptic_release(false) == ESP_OK && control_pin == 5 && control_level == 0);
  assert(board_pins_set_usb_enable(true) == ESP_OK && control_pin == 4 && control_level == 0);
  assert(board_pins_set_usb_enable(false) == ESP_OK && control_pin == 4 && control_level == 1);
  assert(board_pins_haptic_int_high());
  assert(!board_pins_haptic_irq_pending());
  irq_handler(NULL);
  assert(board_pins_haptic_irq_pending());
  assert(board_pins_haptic_irq_pending());
  assert(board_pins_set_haptic_release(false) == ESP_OK);
  assert(!board_pins_haptic_irq_pending());
#endif
#ifndef GPIO_HAPTIC_INT_PIN
  download_test = 1;
  for (int failure = -1; failure < 3; failure++) {
    download_calls = 0; download_fail = failure; reset_delay = 0;
    boot_level = reset_level = 0;
    assert(board_pins_download_c5() == (failure == -1 ? ESP_OK : ESP_FAIL));
    assert(boot_level == (failure == -1 ? 1 : 0));
    assert(reset_level == (failure == 2 ? 1 : 0));
    assert(download_calls == (failure == 0 ? 3 : failure == -1 ? 3 : 4));
  }
  download_test = 0;
  reset_test = 1;
  for (int failure = -1; failure < 3; failure++) {
    reset_calls = 0; reset_delay = 0; reset_fail = failure;
    assert(board_pins_reset_c5() == (failure == -1 ? ESP_OK : ESP_FAIL));
    assert(reset_calls == (failure == 0 ? 1 : 3));
    assert(reset_delay == (failure == 0 ? 0 : failure == 1 ? 10 : 30));
  }
#endif
  return 0;
}
''' 
            (tmp / "test.c").write_text(harness)
            command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(tmp), "-I", str(source), "-I", str(source / "include"),
                       str(tmp / "test.c"), "-o", str(tmp / "test")]
            subprocess.run(command, check=True)
            subprocess.run([str(tmp / "test")], check=True)
            print(f"PASS: {target} V2.03 boot levels, latch-before-output and every GPIO failure")


if __name__ == "__main__":
    main()
