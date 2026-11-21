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

"""Check the production BLE/HID advertisement settings against packet-size and client limits."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nfc_api"))
from test_emu_tx import extract
components = ROOT / "firmware_c5/components"
source = (components / "Service/bluetooth/bluetooth_service.c").read_text()
hid = (components / "Applications/bluetooth/ble_hid_keyboard.c").read_text()
functions = extract(source, "esp_err_t bluetooth_service_start_advertising(void) {") + "\n" + extract(hid, "static esp_err_t hid_start_advertising(void) {")
fixture = Path(__file__).with_suffix(".c").read_text().replace("/* FUNCTIONS */", functions)
with tempfile.TemporaryDirectory(prefix="highboy-announce-settings-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", str(path / "test.c"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=20)
