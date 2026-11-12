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

"""Actual Classic ACL/transport helpers; no RF, keys, device or firmware writes."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

from source import function

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "firmware_p4/components/Drivers/st25r3916"


def main():
    source = (ROOT / "firmware_p4/components/Applications/nfc/protocols/mifare/mf_classic_emu.c").read_text()
    signatures = (
        "static esp_err_t read_irq(", "static int block_to_sector(",
        "static int sector_first_block(", "static int block_index_in_sector(",
        "bool mfc_emu_get_access_bits(", "static bool access_key_allowed(",
        "static bool trailer_can_write_keys(", "static bool trailer_can_write_access(",
        "bool mfc_emu_can_read(", "bool mfc_emu_can_write(",
        "bool mfc_emu_can_increment(", "bool mfc_emu_can_decrement(",
        "static hb_nfc_err_t target_tx_raw(", "static int fifo_rx(uint8_t *buf, size_t buf_max) {",
        "static int target_rx_irq(",
    )
    production = "\n".join(function(source, name) for name in signatures)
    constants = "\n".join(re.findall(r"^#define (?:MFC_|MF_)[^\n]+", source, re.M))
    highboy = (DRIVER / "highboy_nfc.c").read_text()
    production += "\n" + function(highboy, "esp_err_t highboy_nfc_measure_amplitude_checked(")
    # The real state machine must consume preserved snapshots, never treat FWL as RXE.
    step = function(source, "mfc_emu_state_t mfc_emu_run_step(")
    assert "ST25R3916_IRQ_MAIN_FWL" not in step
    assert "s_pending_irq = irq;" in step
    assert step.count("read_irq(&irq)") == 4
    write = function(source, "static mfc_emu_state_t handle_write_phase2(const uint8_t *data, int len) {")
    assert "len != 18" in write
    assert "trailer_can_write_keys" in write and "trailer_can_write_access" in write
    aat = function((DRIVER / "st25r3916_aat.c").read_text(),
                   "esp_err_t st25r3916_aat_calibrate(")
    assert "return ESP_ERR_NOT_SUPPORTED;" in aat
    assert "st25r3916_core_field_on()" not in aat
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* CONSTANTS */", constants).replace("/* PRODUCTION */", production)
    with tempfile.TemporaryDirectory(prefix="tos-classic-emu-") as tmp:
        directory = Path(tmp)
        (directory / "test.c").write_text(fixture)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(DRIVER / "include"), str(directory / "test.c"),
            "-o", str(directory / "test"),
        ], check=True, timeout=60)
        subprocess.run([str(directory / "test")], check=True, timeout=30)


if __name__ == "__main__":
    main()
