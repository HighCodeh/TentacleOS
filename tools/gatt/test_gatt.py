#!/usr/bin/env python3
"""Exercise the production C5 GATT state machine with explicit NimBLE/event fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]

def main():
    source = ROOT / "firmware_c5/components/Applications/bluetooth"
    with tempfile.TemporaryDirectory(prefix="tos-gatt-") as tmp:
        binary = Path(tmp) / "test"
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        for directory in (ROOT / "tools/gatt/fixtures", ROOT / "tools/mqtt/fixtures",
                          source / "include", ROOT / "firmware_c5/components/Service/spi_bridge/include"):
            command += ["-I", str(directory)]
        base_command = command.copy()
        command += [str(source / "gatt_explorer.c"), str(ROOT / "tools/gatt/test_backend.c"), "-o", str(binary)]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)
        p4 = ROOT / "firmware_p4/components"
        command = base_command[:1] + ["-I", str(p4 / "Service/spi_bridge/include")] + base_command[1:] + [
                   str(p4 / "Applications/bluetooth/gatt_explorer.c"),
                   str(ROOT / "tools/gatt/test_proxy.c"), "-o", str(binary)]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)
    manager = (ROOT / "firmware_p4/components/Applications/ui/ui_manager.c").read_text()
    menu = (ROOT / "firmware_p4/components/Applications/ui/screens/bluetooth/ble_detect_ui.c").read_text()
    assert "case SCREEN_GATT_EXPLORER:" in manager and "return ui_gatt_explorer_open;" in manager
    assert "SCREEN_GATT_EXPLORER" in menu

if __name__ == "__main__":
    main()
