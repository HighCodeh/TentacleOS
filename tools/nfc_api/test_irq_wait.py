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

"""Regression: preserve RX during TX wait and bound waits by elapsed SPI time."""
from pathlib import Path
import re
import subprocess
import tempfile
from test_emu_tx import ROOT, extract

src = ROOT / "firmware_p4/components/Drivers/st25r3916"
irq = (src / "st25r3916_irq.c").read_text()
fifo = (src / "st25r3916_fifo.c").read_text()
production = "\n".join(extract(irq, name) for name in (
    "esp_err_t st25r3916_irq_read(", "esp_err_t st25r3916_irq_clear(",
    "esp_err_t st25r3916_irq_wait_txe(", "esp_err_t st25r3916_irq_wait_rxe("))
production += "\n" + extract(fifo, "esp_err_t st25r3916_fifo_wait(")
fixture = Path(__file__).with_suffix(".c").read_text()
constants = "\n".join(re.findall(r"^#define (?:IRQ|FIFO)_US_PER_MS.*", irq + fifo, re.M))
fixture = fixture.replace("/* PRODUCTION */", constants + "\n" + production)
with tempfile.TemporaryDirectory(prefix="nfc-irq-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    (path / "esp_err.h").write_text("typedef int esp_err_t;\n")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(path), "-I" + str(src / "include"),
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
