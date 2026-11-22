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

"""Exercise production UDP probes and SPI dispatch with fake sockets only."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
WIFI = ROOT / "firmware_c5/components/Service/wifi"
NOTICE = (WIFI / "wifi_udp_scan.c").read_text().split("#include", 1)[0]
with tempfile.TemporaryDirectory(prefix="highboy-udp-scan-") as tmp:
    work = Path(tmp)
    for name in ("esp_log.h", "esp_heap_caps.h", "esp_random.h", "esp_timer.h", "lwip/inet_chksum.h", "lwip/sockets.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE + "#pragma once\n")
    (work / "esp_err.h").write_text(NOTICE + "#pragma once\ntypedef int esp_err_t;\n"
        "#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n"
        "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n")
    (work / "esp_rom_crc.h").write_text(NOTICE + "#pragma once\nstatic uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *data, uint32_t len) { (void)data; (void)len; return crc; }\n")
    code = Path(__file__).with_suffix(".c").read_text()
    code = code.replace("/* SOURCE */", '#include "' + str(WIFI / "wifi_udp_scan.c") + '"')
    dispatcher = (WIFI.parent / "spi_bridge/wifi_dispatcher.c").read_text()
    begin = dispatcher.index("#define PORT_SCAN_MAX_RESULTS")
    declarations = dispatcher[begin:dispatcher.index("static portMUX_TYPE s_probe_lock", begin)]
    declarations += "static port_scan_dispatch_req_t s_port_scan_req;\nstatic spi_port_scan_result_t s_port_scan_records[PORT_SCAN_MAX_RESULTS];\nstatic uint16_t s_port_scan_count;\n"
    for signature in ("static void port_scan_hit(", "static void udp_scan_hit(", "static void udp_scan_worker(", "static bool parse_port_list("):
        begin = dispatcher.index(signature)
        declarations += dispatcher[begin:dispatcher.index("\n}\n", begin) + 3]
    begin = dispatcher.index("  if (((id >= SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE")
    guard = dispatcher[begin:dispatcher.index("\n\n", begin)]
    begin = dispatcher.index("    case SPI_ID_WIFI_UDP_SCAN_START:")
    cases = dispatcher[begin:dispatcher.index("    case SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE:", begin)]
    begin = dispatcher.index("    case SPI_ID_WIFI_PORT_SCAN_STOP:")
    cases += dispatcher[begin:dispatcher.index("    case SPI_ID_WIFI_GET_MAC:", begin)]
    code = code.replace("/* DISPATCH_DECLS */", declarations).replace("/* DISPATCH_GUARD */", guard).replace("/* DISPATCH_CASES */", cases)
    (work / "test.c").write_text(code)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(work), "-I" + str(WIFI / "include"), "-I" + str(WIFI.parent / "spi_bridge/include"),
        "-I" + str(ROOT / "firmware_c5/components/Applications/wifi/include"),
        str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, timeout=30)
