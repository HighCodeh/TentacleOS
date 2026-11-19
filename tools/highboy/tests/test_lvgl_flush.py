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


"""Exercise the production display glue with blocking semaphore/ISR scheduling."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "firmware_p4/components/Service/lvgl/lvgl_glue.c"
NOTICE = SOURCE.read_text().split("#include", 1)[0]

with tempfile.TemporaryDirectory(prefix="highboy-lvgl-flush-") as directory:
    work = Path(directory)
    for name in ("lvgl_glue.h", "freertos/FreeRTOS.h", "freertos/semphr.h",
                 "esp_heap_caps.h", "esp_lcd_panel_io.h", "esp_lvgl_port.h", "esp_log.h",
                 "draw/lv_draw_buf_private.h", "spi.h", "st7789.h", "sys_prio.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE)
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* DISPLAY_GLUE */", SOURCE.read_text())
    test = work / "test.c"
    test.write_text(fixture)
    executable = work / "test"
    command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-pthread",
               "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-I" + str(work), str(test), "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True, timeout=20)
