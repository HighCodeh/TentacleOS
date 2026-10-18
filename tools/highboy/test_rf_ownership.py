#!/usr/bin/env python3
"""Exercise production RF ownership and RFID cancellation with explicit host fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


def main():
    core = (ROOT / "firmware_p4/components/Drivers/st25r3916/st25r3916_core.c").read_text()
    hal = (ROOT / "firmware_p4/components/Drivers/st25r3916/hal/hb_nfc_spi.c").read_text()
    ui = (ROOT / "firmware_p4/components/Applications/ui/screens/rfid/rfid_menu_ui.c").read_text()
    frontend = (ROOT / "firmware_p4/components/Drivers/rf_frontend/rf_frontend.c").read_text()
    # Hardware and scheduling are faked; the lock is a real host mutex.
    common = (ROOT / "tools/highboy/tests/rf_ownership_test.c").read_text()
    core_functions = "\n".join(function(core, signature) for signature in (
        "esp_err_t st25r3916_core_field_off(void)",
        "esp_err_t st25r3916_core_field_on(void)",
        "esp_err_t st25r3916_core_deinit(void)",
    ))
    ui_states = ui[ui.index("static portMUX_TYPE s_reader_lock"):ui.index("static button_ui_t s_info_emulate")]
    ui_functions = ui[ui.index("static bool reader_is_current"):ui.index("static void stop_scan_timers")]
    source = common.replace("/* FRONTEND */", frontend).replace(
        "/* HAL */", function(hal, "esp_err_t hb_nfc_spi_reg_write(uint8_t addr, uint8_t value)"))
    source = source.replace("/* CORE */", core_functions).replace("/* RFID */", ui_states + ui_functions)
    with tempfile.TemporaryDirectory(prefix="highboy-rf-test-") as temporary:
        directory = Path(temporary)
        for header in ("esp_err.h", "esp_log.h", "freertos/FreeRTOS.h"):
            target = directory / header
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("/* Host interfaces supplied by the harness. */\n")
        harness = directory / "test.c"
        harness.write_text(source)
        executable = directory / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=undefined", "-pthread", "-I", str(directory),
                        "-I", str(ROOT / "firmware_p4/components/Drivers/rf_frontend/include"),
                        str(harness), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    main()
