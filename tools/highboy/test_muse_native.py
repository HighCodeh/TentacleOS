#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
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

"""Run native P4 Muse adapter failure paths with explicit device interface fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MUSE = ROOT / "firmware_p4/components/Service/muse_port"


def main():
    with tempfile.TemporaryDirectory(prefix="highboy-native-muse-") as temporary:
        directory = Path(temporary)
        (directory / "native_host.h").write_text("/* Interfaces supplied by muse_native_test.c. */\n")
        (directory / "esp_rom_crc.h").write_text(
            "#include <stdint.h>\nuint16_t esp_rom_crc16_le(uint16_t, const uint8_t *, uint32_t);\n")
        executable = directory / "test"
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                   "-Werror", "-fsanitize=undefined", "-DHB_MUSE_P4_HOST_TEST", "-I", str(directory),
                   "-I", str(MUSE / "p4_app"),
                   "-I", str(ROOT / "firmware_p4/components/Service/spi_bridge/include"),
                   str(ROOT / "tools/highboy/tests/muse_native_test.c")]
        command += [str(MUSE / source) for source in (
            "p4_app/hb_muse_app.c", "core/hb_muse_core.c", "transport/hb_muse_wire.c",
            "transport/hb_muse_wire_client.c")]
        subprocess.run([*command, "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    main()
