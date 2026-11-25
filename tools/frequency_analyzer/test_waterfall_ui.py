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

import argparse
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "firmware_p4/components/Applications/ui"
SERVICE = ROOT / "firmware_p4/components/Service/frequency_analyzer"
parser = argparse.ArgumentParser()
parser.add_argument("--host-build", type=Path, default=ROOT.parent / "host-ui")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(exist_ok=True, parents=True)
with tempfile.TemporaryDirectory(prefix="frequency-ui-") as directory:
    directory = Path(directory)
    for name in ["esp_heap_caps.h", "subghz_menu_ui.h", "led_control.h", "power_policy.h", "ui_chrome.h", "ui_feedback.h", "ui_manager.h", "ui_metrics.h"]:
        (directory / name).write_text("#pragma once\n")
    (directory / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_INVALID_STATE 0x103\n")
    (directory / "esp_log.h").write_text("#pragma once\n#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n")
    notice = "\n".join("//" + line[1:] for line in Path(__file__).read_text().splitlines()[:14]) + "\n\n"
    for header in directory.rglob("*.h"):
        header.write_text(notice + header.read_text())
    includes = [directory, args.host_build, SERVICE / "include", UI / "include", UI / "screens/subghz",
                UI / "screens/subghz/include", ROOT / "firmware_p4/components/Applications/SubGhz/include",
                ROOT / "firmware_p4/components/Drivers/cc1101/include", ROOT / "firmware_p4/managed_components/lvgl__lvgl"]
    binary = directory / "preview"
    subprocess.run(["cc", "-std=gnu11", "-fsanitize=undefined", "-g", "-Wall", "-Wextra",
                    "-Wno-unused-parameter", *["-I" + str(p) for p in includes],
                    str(Path(__file__).with_suffix(".c")), str(SERVICE / "frequency_waterfall_plan.c"), str(SERVICE / "frequency_waterfall_color.c"),
                    str(args.host_build / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(args.output.resolve())], check=True, timeout=45)
try:
    from PIL import Image
except ImportError:
    Image = None
notice = (UI / "screens/subghz/subghz_frequency_ui.c").read_text().split("#include", 1)[0]
for file in args.output.glob("*.ppm"):
    artifacts = [file]
    if Image is not None:
        Image.open(file).save(file.with_suffix(".png"))
        artifacts.append(file.with_suffix(".png"))
    for artifact in artifacts:
        artifact.with_name(artifact.name + ".license").write_text(notice)
