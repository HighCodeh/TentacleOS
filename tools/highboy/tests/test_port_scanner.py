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

"""Run production scanner with a simulated SPI peer, never a network."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
SERVICE = ROOT / "firmware_p4/components/Service"
NOTICE = (SERVICE / "port_scanner/port_scanner.c").read_text().split("#include", 1)[0]
with tempfile.TemporaryDirectory(prefix="highboy-port-scanner-", dir="/tmp") as tmp:
    work = Path(tmp)
    for name in ("esp_heap_caps.h", "esp_log.h", "esp_timer.h", "freertos/FreeRTOS.h",
                 "freertos/task.h", "freertos/idf_additions.h", "led_control.h",
                 "power_manager.h", "resource_mgr.h", "spi_bridge.h", "sys_prio.h",
                 "esp_random.h", "storage_init.h", "storage_mkdir.h", "storage_atomic.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE + "#pragma once\n")
    (work / "lwip").mkdir()
    for name in ("def.h", "inet.h", "sockets.h"):
        (work / "lwip" / name).write_text(NOTICE + "#include <arpa/inet.h>\n")
    (work / "esp_err.h").write_text(NOTICE + "#pragma once\ntypedef int esp_err_t;\n")
    (work / "esp_rom_crc.h").write_text(NOTICE + "#pragma once\n#include <stdint.h>\nstatic inline uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *data, uint32_t len) { (void)data; (void)len; return crc; }\n")
    store_header = (SERVICE / "port_scanner/include/port_scanner_store.h").read_text()
    (work / "port_scanner_store.h").write_text(store_header.replace(
        '"/sdcard/wifi/port_scans"', '"' + str(work / "scans") + '"'))
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* ATOMIC */", '#include "' + str(SERVICE / "storage_api/storage_atomic.c") + '"')
    fixture = fixture.replace("/* STORE */", '#include "' + str(SERVICE / "port_scanner/port_scanner_store.c") + '"')
    console = (SERVICE / "console/commands/cmd_wifi.c").read_text()
    start = console.index("static int subcmd_portscan(")
    end = console.index("\n// --- STATUS", start)
    fixture = fixture.replace("/* CONSOLE */", console[start:end])
    (work / "test.c").write_text(fixture.replace("/* PRODUCTION */", '#include "' + str(SERVICE / "port_scanner/port_scanner.c") + '"'))
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I" + str(work),
                    "-I" + str(SERVICE / "port_scanner/include"),
                    "-I" + str(SERVICE / "spi_bridge/include"),
                    "-I" + str(ROOT / "firmware_p4/components/Applications/wifi/include"),
                    str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, timeout=30)
