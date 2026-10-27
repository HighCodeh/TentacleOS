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

"""Fault-inject the production queue ownership and recovery supervisor on the host."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    driver = (ROOT / "firmware_c5/components/Drivers/spi_slave/spi_slave_driver.c").read_text()
    bridge = (ROOT / "firmware_c5/components/Service/spi_bridge/spi_bridge.c").read_text()
    codec = (HERE / "test_transport.c").read_text()
    production = codec[codec.index("uint16_t esp_rom_crc16_le"):codec.index("/* PRODUCTION */")]
    production += driver[driver.index("#define SPI_PENDING_COUNT"):driver.index("static void IRAM_ATTR post_setup_observe")]
    production += driver[driver.index("static bool pair_buffer_valid"):driver.index("esp_err_t spi_slave_driver_init")]
    production += driver[driver.index("esp_err_t spi_slave_driver_queue("):]
    production += bridge[bridge.index("#define SPI_RECOVERY_TIMEOUT_MS"):bridge.index("typedef enum {\n  SPI_TRANSPORT_DROP")]
    fixture = (HERE / "test_recovery.c").read_text().replace("/* PRODUCTION */", production)
    with tempfile.TemporaryDirectory(prefix="spi-recovery-") as tmp:
        source = Path(tmp) / "test.c"
        binary = Path(tmp) / "test"
        source.write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "tools/mqtt/fixtures"),
                        "-I", str(ROOT / "firmware_p4/components/Service/spi_bridge/include"),
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)

if __name__ == "__main__":
    main()
