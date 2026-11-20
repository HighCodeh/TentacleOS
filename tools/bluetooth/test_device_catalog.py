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

"""Compile the real P4/C5 BLE encoders with deterministic RNG; no radio access."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    catalog = (ROOT / "docs/bluetooth/DEVICE_CATALOG.md").read_text()
    groups = (("Fast Pair", "fastpair", 498), ("Samsung Watch", "watch", 30),
              ("Samsung Buds", "buds", 20), ("Apple Proximity Pair", "apple", 19))
    expected = "#include <stdint.h>\n"
    for title, name, count in groups:
        section = catalog.split("## " + title + "\n", 1)[1].split("\n## ", 1)[0]
        values = re.findall(r"\| `(0x[0-9A-F]+)` \|", section)
        assert len(values) == count and len(set(values)) == count, title
        expected += "static const uint32_t expected_" + name + "[] = {" + ",".join(values) + "};\n"
    with tempfile.TemporaryDirectory(prefix="tos-ble-catalog-") as temp:
        work = Path(temp)
        (work / "expected.h").write_text(expected)
        (work / "esp_random.h").write_text("#include <stdint.h>\nuint32_t esp_random(void);\n")
        for chip in ("p4", "c5"):
            sources = ROOT / ("firmware_" + chip) / "components/Applications/bluetooth"
            binary = work / ("catalog-" + chip)
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I" + str(work), "-I" + str(sources), "-I" + str(sources / "include"),
                str(HERE / "test_device_catalog.c"), "-o", str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
            print(chip + ": all catalog entries, wire bytes and buffer boundaries passed")


if __name__ == "__main__":
    main()
