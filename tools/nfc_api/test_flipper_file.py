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

"""Compile the actual Classic Flipper file parser with sanitized host I/O tests."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc"


def main():
    with tempfile.TemporaryDirectory(prefix="nfc-file-") as tmp:
        directory = Path(tmp)
        (directory / "esp_err.h").write_text("typedef int esp_err_t;\n" + "\n".join(
            f"#define {name} {index}" for index, name in enumerate([
                "ESP_OK", "ESP_FAIL", "ESP_ERR_INVALID_ARG", "ESP_ERR_NOT_FOUND",
                "ESP_ERR_INVALID_RESPONSE"])))
        (directory / "esp_log.h").write_text("#define ESP_LOGW(tag, ...) ((void)(tag))\n")
        (directory / "mf_classic_emu.h").write_text("""
#include <stdint.h>
#include <stdbool.h>
#define MFC_EMU_MAX_BLOCKS 256
#define MFC_EMU_BLOCK_SIZE 16
#define MF_CLASSIC_MINI 0
#define MF_CLASSIC_1K 1
#define MF_CLASSIC_4K 2
typedef struct {
 uint8_t uid[10], uid_len, atqa[2], sak;
 int type;
 uint8_t blocks[256][16];
 int total_blocks;
 struct {uint8_t key_a[6], key_b[6]; bool key_a_known, key_b_known;} keys[40];
 int sector_count;
} mfc_emu_card_data_t;
""")
        exe = directory / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", "-I" + str(directory),
                        "-I" + str(BASE / "include"), str(BASE / "nfc_flipper_file.c"),
                        str(Path(__file__).with_suffix(".c")), "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(directory / "card.nfc")], check=True)


if __name__ == "__main__":
    main()
