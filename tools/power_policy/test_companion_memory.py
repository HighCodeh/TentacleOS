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

"""Exercise C5 PSRAM allocation failures and queue ownership without hardware."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    bridge = (ROOT / "firmware_c5/components/Service/spi_bridge/spi_bridge.c").read_text()
    scans = (ROOT / "firmware_c5/components/Service/spi_bridge/wifi_dispatcher.c").read_text()
    def function(source, signature):
        start = source.rindex(signature)
        end = source.index("{", start) + 1
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[start:end] + "\n"
    production = "\n".join(line for line in bridge.splitlines() if line.startswith(("#define SPI_STREAM_QUEUE_LEN", "#define SPI_BRIDGE_TASK_STACK"))) + "\n"
    production += bridge[bridge.index("typedef struct {\n  spi_id_t id;"):bridge.index("static void *s_data_source")]
    production += bridge[bridge.index("static spi_stream_item_t *s_stream_queue;"):bridge.index("static bool s_is_wifi_sniffer_streaming")]
    production += scans[scans.index("typedef struct {\n  spi_wifi_scan_record_t records"):scans.index("static spi_wifi_scan_config_t")]
    for signature in ("esp_err_t spi_bridge_slave_init_mode(", "bool spi_bridge_stream_push(", "static uint16_t stream_pop_into("):
        production += function(bridge, signature)
    fixture = (HERE / "test_companion_memory.c").read_text().replace("/* PRODUCTION */", production)
    with tempfile.TemporaryDirectory(prefix="companion-memory-") as directory:
        source = Path(directory) / "test.c"
        binary = Path(directory) / "test"
        source.write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "tools/mqtt/fixtures"),
                        "-I", str(ROOT / "firmware_c5/components/Service/spi_bridge/include"),
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)

if __name__ == "__main__":
    main()
