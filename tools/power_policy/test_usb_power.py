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

"""Run the production USB input worker against source and charger faults."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/highboy/tests"))
from test_shutdown import MOCKS, NOTICE

with tempfile.TemporaryDirectory(prefix="highboy-usb-power-") as directory:
    work = Path(directory)
    mocks = dict(MOCKS)
    mocks.pop("husb320.h", None)
    mocks["freertos/FreeRTOS.h"] += "#define pdPASS 1\n"
    mocks["freertos/task.h"] += "typedef void *TaskHandle_t;\nint xTaskCreatePinnedToCore(void (*)(void *),const char *,uint32_t,void *,int,void **,int);\n"
    mocks["led_control.h"] = "void led_signal_warning(void);void led_signal_info(void);void led_signal_error(void);\n"
    mocks["sys_prio.h"] = "#define SYS_PRIO_SERVICE_HI 5\n#define SYS_CORE_RADIO 0\n"
    for name, body in mocks.items():
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE + body)
    includes = [work, ROOT / "firmware_p4/components/Service/power_manager/include",
                ROOT / "firmware_p4/components/Drivers/husb320/include",
                ROOT / "firmware_p4/components/Drivers/bq25896/include"]
    command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
               "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               *["-I" + str(path) for path in includes], str(Path(__file__).with_suffix(".c")),
               "-o", str(work / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "test")], check=True, timeout=30)
