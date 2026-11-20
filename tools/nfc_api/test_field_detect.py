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

"""Exercise the production passive-field diagnostic with injected SPI failures."""
from pathlib import Path
import re, subprocess, tempfile
from test_emu_tx import ROOT, extract
source = (ROOT / "firmware_p4/components/Drivers/st25r3916/highboy_nfc.c").read_text()
constants = "\n".join(re.findall(r"^#define HIGHBOY_NFC_[^\n]+", source, re.M))
function = extract(source, "esp_err_t highboy_nfc_detect_external_field(")
fixture = Path(__file__).with_suffix(".c").read_text().replace("/* CONSTANTS */", constants).replace("/* PRODUCTION */", function)
with tempfile.TemporaryDirectory(prefix="nfc-field-") as tmp:
    p = Path(tmp)
    (p / "test.c").write_text(fixture)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I" + str(ROOT / "firmware_p4/components/Drivers/st25r3916/include"), str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
