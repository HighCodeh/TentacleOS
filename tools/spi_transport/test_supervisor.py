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

"""Run production P4 recovery policy and exclusion gate with fault injection."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    manager = (ROOT / "firmware_p4/components/Service/bridge_manager/bridge_manager.c").read_text()
    bridge = (ROOT / "firmware_p4/components/Service/spi_bridge/spi_bridge.c").read_text()
    policy = manager[manager.index("#define VERSION_BUF_SIZE"):]
    gate = bridge[bridge.index("#define SPI_RECOVERY_BOOT_GRACE_MS"):bridge.index("static atomic_uint s_stream_epoch;")]
    gate += bridge[bridge.index("void spi_bridge_recovery_disable(void)"):bridge.index("void spi_bridge_suspend(bool suspended)")]
    flasher = (ROOT / "firmware_p4/components/Service/c5_flasher/c5_flasher.c").read_text()
    flasher = flasher[flasher.index('static const char *TAG'):]
    with tempfile.TemporaryDirectory(prefix="p4-supervisor-") as directory:
        for name, production in (("supervisor", policy), ("recovery_gate", gate), ("flasher_guard", flasher)):
            source = Path(directory) / "test.c"
            source.write_text((HERE / f"test_{name}.c").read_text().replace("/* PRODUCTION */", production))
            binary = Path(directory) / "test"
            subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-I", str(ROOT / "tools/mqtt/fixtures"),
                            "-I", str(ROOT / "firmware_p4/components/Service/spi_bridge/include"),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)

if __name__ == "__main__":
    main()
