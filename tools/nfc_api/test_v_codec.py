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

"""Check the production NFC-V transparent codec with ASan/UBSan."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Drivers/st25r3916/hal"


def main():
    with tempfile.TemporaryDirectory(prefix="nfc-v-codec-") as tmp:
        directory = Path(tmp)
        (directory / "esp_err.h").write_text("typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n")
        (directory / "esp_log.h").write_text("#define ESP_LOGE(tag, ...) ((void)(tag))\n")
        exe = directory / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", "-I" + str(directory),
                        "-I" + str(BASE / "include"), str(BASE / "hb_nfc_v_codec.c"),
                        str(Path(__file__).with_suffix(".c")), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
