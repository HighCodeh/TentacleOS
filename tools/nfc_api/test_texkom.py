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

"""Compile and exercise the production Texkom decoder with sanitizers."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/"firmware_p4/components/Applications/nfc/protocols/texkom"
with tempfile.TemporaryDirectory(prefix="texkom-") as temp:
    p=Path(temp)
    notice=Path(__file__).with_suffix(".c").read_text().split("#include")[0]
    (p/"esp_err.h").write_text(notice+"#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_RESPONSE 2\ntypedef int esp_err_t;\n")
    (p/"esp_log.h").write_text(notice+"#include <stdbool.h>\n#define ESP_LOGD(tag, ...) ((void)(tag))\n")
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all","-include","stdbool.h","-I",str(p),"-I",str(BASE/"include"),
                    str(BASE/"texkom.c"),str(Path(__file__).with_suffix(".c")),"-o",str(p/"test")],check=True)
    subprocess.run([str(p/"test")],check=True)
