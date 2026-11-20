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

"""Exercise the production RX worker with explicit RMT/RTOS/resource fakes."""
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / "firmware_p4/components/Applications/SubGhz"
NOTICE = (HERE / "test_subghz_rx_fakes.h").read_text().split("#ifndef", 1)[0]


def main():
    with tempfile.TemporaryDirectory(prefix="subghz-lifecycle-") as temporary:
        work = Path(temporary)
        headers = ["driver/gpio.h", "driver/rmt_rx.h", "driver/rmt_encoder.h",
                   "esp_log.h", "esp_heap_caps.h", "freertos/FreeRTOS.h", "freertos/task.h",
                   "freertos/queue.h", "freertos/semphr.h", "pin_def.h",
                   "led_control.h", "subghz_protocol_registry.h", "subghz_storage.h",
                   "subghz_transmitter.h", "resource_mgr.h", "sys_prio.h"]
        for name in headers:
            target = work / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(NOTICE + '#include "rx_fakes.h"\n')
        (work / "rx_fakes.h").write_text((HERE / "test_subghz_rx_fakes.h").read_text())
        errors = (HERE / "test_subghz_rx_fakes.h").read_text().splitlines()
        (work / "esp_err.h").write_text(NOTICE + "#pragma once\n" + "\n".join(
            line for line in errors if line.startswith("typedef int esp_err_t") or line.startswith("#define ESP_") and not line.startswith("#define ESP_LOG")))
        binary = work / "rx_test"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-Wno-unused-parameter", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", "-g", "-I", str(work), "-I", str(SOURCE),
                        "-I", str(SOURCE / "include"),
                        "-I", str(ROOT / "firmware_p4/components/Drivers/cc1101/include"),
                        str(HERE / "test_subghz_lifecycle.c"),
                        str(ROOT / "firmware_p4/components/Drivers/cc1101/cc1101_config.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
