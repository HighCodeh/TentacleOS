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

"""Run the real P4 settings service with transport, task and storage fault injection."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / "firmware_p4/components/Service"
NOTICE = (SERVICE / "connection_settings/connection_settings.c").read_text().split("#include", 1)[0]
with tempfile.TemporaryDirectory(prefix="highboy-connections-service-") as tmp:
    work = Path(tmp)
    for name in ("tos_flash_paths.h", "esp_heap_caps.h", "esp_log.h", "freertos/FreeRTOS.h",
                 "freertos/idf_additions.h", "freertos/task.h", "bluetooth_service.h",
                 "led_control.h", "sys_prio.h", "tos_storage_paths.h", "wifi_service.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE + "#pragma once\n")
    (work / "esp_err.h").write_text(NOTICE + "#pragma once\ntypedef int esp_err_t;\n")
    fixture = Path(__file__).with_suffix(".c").read_text()
    (work / "test.c").write_text(fixture.replace("/* PRODUCTION */", '#include "' + str(SERVICE / "connection_settings/connection_settings.c") + '"'))
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + str(work),
                    "-I" + str(SERVICE / "connection_settings/include"),
                    "-I" + str(SERVICE / "storage_api/include"),
                    str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, timeout=20)
