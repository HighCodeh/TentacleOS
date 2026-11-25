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

from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / "firmware_p4/components/Service/frequency_analyzer"
with tempfile.TemporaryDirectory(prefix="frequency-analyzer-") as directory:
    directory = Path(directory)
    (directory / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n" + "\n".join(
        f"#define {name} {value}" for name, value in {
            "ESP_OK": 0, "ESP_FAIL": -1, "ESP_ERR_NO_MEM": 0x101,
            "ESP_ERR_INVALID_ARG": 0x102, "ESP_ERR_INVALID_STATE": 0x103,
            "ESP_ERR_TIMEOUT": 0x107}.items()) + "\n")
    for name in ["esp_log.h", "esp_timer.h", "freertos/FreeRTOS.h", "freertos/task.h", "sys_prio.h", "led_control.h"]:
        path = directory / name
        path.parent.mkdir(exist_ok=True, parents=True)
        path.write_text('#include "test_platform.h"\nvoid led_signal_error(void);\n')
    notice = "\n".join("//" + line[1:] for line in Path(__file__).read_text().splitlines()[:14]) + "\n\n"
    for header in directory.rglob("*.h"):
        header.write_text(notice + header.read_text())
    includes = [directory, ROOT / "tools/subghz_api/fixtures", SERVICE, SERVICE / "include",
                ROOT / "firmware_p4/components/Drivers/cc1101/include",
                ROOT / "firmware_p4/components/Core/resource_mgr/include"]
    binary = directory / "test"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-pthread",
                    *["-I" + str(p) for p in includes], str(Path(__file__).with_suffix(".c")),
                    "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
