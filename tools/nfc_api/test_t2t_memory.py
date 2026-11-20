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

"""Exercise production Type 2 memory handling with ASan/UBSan."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from test_emu_tx import extract
from test_readers import defines
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc"


def main():
    source = (BASE / "protocols/t2t/t2t_emu.c").read_text()
    names = ["static bool is_i2c(void) {", "static void i2c_select(", "static bool i2c_address(", "static void i2c_read(", "static bool i2c_write(", "static void aes_identity(void) {", "static bool is_aes(void) {", "static uint16_t get_static_lock_bits(void) {",
             "static uint16_t get_dynamic_lock_bits(void) {",
             "static bool is_page_locked(uint16_t page) {",
             "static bool is_page_protected(uint16_t page) {",
             "static bool can_read_page(uint16_t page) {",
             "static bool can_write_page(uint16_t page) {",
             "static void otp_write_bytes(",
             "static void ulc_activate(bool is_field_reset) {",
             "static bool ulc_write_counter(const uint8_t *data) {",
             "static bool write_page_internal(uint8_t page, const uint8_t *data) {",
             "static bool is_ev1(void) {",
             "static bool is_ntag(void) {",
             "static void handle_tearing(uint8_t index) {",
             "static void handle_increment(const uint8_t *cmd) {",
             "static bool can_read_counter(void) {",
             "static void increase_counter(void) {",
             "static void handle_counter(uint8_t index) {",
             "static void handle_write_signature(const uint8_t *frame, size_t length) {",
             "static void handle_lock_signature(const uint8_t *frame, size_t length) {",
             "static void handle_signature(uint8_t parameter, size_t length) {",
             "static void handle_vcsl(size_t length) {",
             "static void apply_mirror(uint16_t page, uint8_t *out) {",
             "static void handle_pwd_auth(const uint8_t *cmd, int len) {",
             "static void copy_read_page(",
             "static void handle_read(uint8_t page) {",
             "static void handle_fast_read(uint8_t start, uint8_t end) {",
             "static void handle_comp_write_start(uint8_t page) {",
             "static void handle_comp_write_data(const uint8_t *data, int len) {"]
    # Use definitions rather than forward declarations.
    code = "\n".join(extract(source[source.rindex(name):], name) for name in names)
    fixture = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", code)
    header = (BASE / "protocols/t2t/include/t2t_emu.h").read_text()
    constants = "\n".join(line for line in (source + header).splitlines() if line.startswith("#define T2T_"))
    lock_enum = header[header.index("typedef enum {", header.index("#define T2T_AES_KEY_COUNT")):header.index("} t2t_signature_lock_t;") + len("} t2t_signature_lock_t;")]
    fixture = fixture.replace("/* DEFINES */", constants + "\n" + lock_enum)
    with tempfile.TemporaryDirectory(prefix="nfc-t2t-") as tmp:
        p = Path(tmp)
        (p / "test.c").write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", str(p / "test.c"),
                        "-o", str(p / "test")], check=True)
        subprocess.run([str(p / "test")], check=True)

if __name__ == "__main__":
    main()
