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
#

"""Exercise production Classic read/save/navigation with real LVGL and deferred workers."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
NFC = ROOT / "firmware_p4/components/Applications/nfc"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "nfc-read-flow"
WORK.mkdir(parents=True, exist_ok=True)
source = (UI / "screens/nfc/nfc_read_ui.c").read_text()
notice = source.split("#include", 1)[0]

def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

for name in ("ui_theme.h", "ui_chrome.h", "ui_metrics.h", "assets_manager.h"):
    (WORK / name).write_text(notice + "#pragma once\n")
(WORK / "esp_log.h").write_text(notice + "#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGD(...) ((void)0)\n")
(WORK / "esp_err.h").write_text(notice + "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n")
headers = "\n".join(re.sub(r"^#include.*$", "", (NFC / "include" / name).read_text(), flags=re.M)
                      for name in ("nfc_mfc_dump.h", "nfc_capabilities.h"))
globals_ = source[source.index("#define REFRESH_MS"):source.index("static void show_rings(bool show);")]
functions = "\n".join(function(signature) for signature in (
    "static bool has_block_data(", "static bool block_cancel_requested(",
    "static void publish_block_result(", "static void block_progress_cb(",
    "static bool block_cancel_cb(", "static void block_read_task(",
    "static void block_save_task(", "static void clear_result_objects(",
    "static void show_block_error(", "static void update_block_view(",
    "static void show_block_result(const block_read_result_t *result) {",
    "static void start_block_read(void) {", "static const char *action_symbol(",
    "static void show_options(void) {", "static void start_block_save(",
    "static void on_name_submit(", "static void begin_scan(void) {", "static void nfc_read_input(",
    "static void refresh_cb(", "static void run_context_action(nfc_card_action_id_t action) {",
    "void ui_nfc_read_stop("))
fixture = Path(__file__).with_suffix(".c").read_text()
fixture = fixture.replace("/* HEADERS */", headers).replace("/* GLOBALS */", globals_).replace("/* FUNCTIONS */", functions)
fixture = fixture.replace("/* CAPTURE UI */", (UI / "components/capture_result/capture_result_ui.c").read_text().replace('static const char *TAG = "CAPTURE_RESULT";', ''))
path = WORK / "test.c"
path.write_text(fixture)
command = ["cc", "-std=gnu11", "-g", "-Werror=return-type", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(WORK), "-I" + str(BASE),
           "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"), "-I" + str(UI / "include"),
           "-I" + str(UI / "screens/nfc/include"), "-I" + str(UI / "components/capture_result/include"),
           "-I" + str(UI / "components/keyboard/include"), str(path),
           str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True)
subprocess.run([str(WORK / "test")], check=True)
