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

"""Exercise the C5 production idle policy and driver sleep transition with SDK fakes."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    driver = (ROOT / "firmware_c5/components/Drivers/spi_slave/spi_slave_driver.c").read_text()
    driver = driver[driver.index("#define SPI_PENDING_COUNT"):driver.index("static bool release_pending(")]
    policy = (ROOT / "firmware_c5/components/Service/power_manager/power_manager.c").read_text()
    policy = policy[policy.index("static const char *TAG"):]
    fixture = (HERE / "test_companion_sleep.c").read_text().replace("/* DRIVER */", driver).replace("/* POLICY */", policy)
    with tempfile.TemporaryDirectory(prefix="companion-sleep-") as directory:
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
