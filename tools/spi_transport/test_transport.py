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

"""Exercise production SPI transport code with an explicit in-memory PHY/clock."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    master = (ROOT / "firmware_p4/components/Service/spi_bridge/spi_bridge.c").read_text()
    slave = (ROOT / "firmware_c5/components/Service/spi_bridge/spi_bridge.c").read_text()
    helpers = master[master.index("// All transport state"):master.index("typedef struct {\n  spi_id_t id;")]
    classifier = slave[slave.index("typedef enum {\n  SPI_TRANSPORT_DROP"):slave.index("static void bridge_task(void *pvParameters) {")]
    fixture = (HERE / "test_transport.c").read_text()
    fixture = fixture.replace("/* PRODUCTION */", classifier + helpers)
    with tempfile.TemporaryDirectory(prefix="spi-transport-") as tmp:
        source = Path(tmp) / "test.c"
        binary = Path(tmp) / "test"
        source.write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "tools/mqtt/fixtures"),
                        "-I", str(ROOT / "firmware_p4/components/Service/spi_bridge/include"),
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
