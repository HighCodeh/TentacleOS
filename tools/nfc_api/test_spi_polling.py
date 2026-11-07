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

"""Exercise production NFC polling transfer ownership and lock cleanup."""
from pathlib import Path
import subprocess
import tempfile
from test_emu_tx import ROOT, extract
source = (ROOT / "firmware_p4/components/Drivers/st25r3916/hal/hb_nfc_spi.c").read_text()
code = extract(source, "static esp_err_t transmit(spi_transaction_t *t) {")
fixture = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", code)
with tempfile.TemporaryDirectory(prefix="nfc-spi-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
