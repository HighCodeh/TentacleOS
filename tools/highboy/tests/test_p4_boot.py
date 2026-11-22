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

"""Execute the P4 console download command with UART and driver failures."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "firmware_p4/components/Service/console/commands/cmd_system.c"


def main():
    source = SOURCE.read_text()
    notice = source.split("#include", 1)[0]
    start = source.index("static int cmd_p4(")
    end = source.index("\nstatic int cmd_firstboot", start)
    defines = "\n".join(re.findall(r"^#define P4_BOOT_.*$", source, re.MULTILINE))
    harness = notice + r'''#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_TIMEOUT, ESP_ERR_NOT_SUPPORTED };
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#define pdMS_TO_TICKS(ms) (ms)
static const char *TAG = "CMD_SYSTEM";
static int phase, restarts, infos, warnings, errors, uart_result, boot_result;
static void log_message(const char *tag, const char *format, ...) {
  assert(tag != NULL);
  char message[256];
  va_list args;
  va_start(args, format);
  assert(vsnprintf(message, sizeof(message), format, args) > 0);
  va_end(args);
}
#define ESP_LOGI(tag, ...) log_message(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) log_message(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) log_message(tag, __VA_ARGS__)
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
static void led_signal_info(void) { ++infos; }
static void led_signal_warning(void) { ++warnings; }
static void led_signal_error(void) { ++errors; }
static esp_err_t uart_wait_tx_done(int port, unsigned ticks) {
  assert(phase == 0 && port == CONFIG_ESP_CONSOLE_UART_NUM && ticks > 0 && ticks <= 1000);
  phase = 1;
  return uart_result;
}
static esp_err_t board_pins_prepare_download_p4(void) {
  assert(phase == 1 && uart_result == ESP_OK);
  phase = 2;
  return boot_result;
}
static void esp_restart(void) {
  assert(phase == 2 && boot_result == ESP_OK);
  phase = 3;
  ++restarts;
}
''' + defines + "\n" + source[start:end] + r'''
int main(void) {
  char *valid[] = {"p4", "boot", NULL};
  char *missing[] = {"p4", NULL};
  char *invalid[] = {"p4", "boots", NULL};
  char *extra[] = {"p4", "boot", "unexpected", NULL};
  assert(cmd_p4(0, NULL) != 0);
  assert(cmd_p4(1, missing) != 0);
  assert(cmd_p4(2, invalid) != 0);
  assert(cmd_p4(3, extra) != 0);
  assert(phase == 0 && restarts == 0 && warnings == 4);

  uart_result = ESP_ERR_TIMEOUT;
  assert(cmd_p4(2, valid) != 0);
  assert(phase == 1 && restarts == 0 && errors == 1);

  phase = 0;
  uart_result = ESP_OK;
  boot_result = ESP_ERR_NOT_SUPPORTED;
  assert(cmd_p4(2, valid) != 0);
  assert(phase == 2 && restarts == 0 && errors == 2);

  phase = 0;
  boot_result = ESP_FAIL;
  assert(cmd_p4(2, valid) != 0);
  assert(phase == 2 && restarts == 0 && errors == 3);

  phase = 0;
  boot_result = ESP_OK;
  assert(cmd_p4(2, valid) == 0);
  assert(phase == 3 && restarts == 1 && infos == 4 && errors == 3);
  return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="highboy-p4-boot-") as temporary:
        work = Path(temporary)
        (work / "test.c").write_text(harness)
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                   "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                   str(work / "test.c"), "-o", str(work / "test")]
        subprocess.run(command, check=True)
        subprocess.run([str(work / "test")], check=True, timeout=30)
    print("PASS: P4 boot arguments, UART drain, denied boot, readback failure and restart order")


if __name__ == "__main__":
    main()
