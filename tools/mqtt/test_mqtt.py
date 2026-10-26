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

"""Build the real MQTT codec/bridge/adapter with explicit RTOS, network and AES fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tools/mqtt/fixtures"
P4 = ROOT / "firmware_p4/components"
C5 = ROOT / "firmware_c5/components"


def main():
    manager = (P4 / "Applications/ui/ui_manager.c").read_text()
    home = (P4 / "Applications/ui/screens/lora/lora_chat_ui.c").read_text()
    assert 'case SCREEN_LORA_MQTT:\n      return ui_lora_mqtt_open;' in manager
    lora_family = manager.split('static bool is_lora_screen(', 1)[1].split('\n}', 1)[0]
    assert 'case SCREEN_LORA_MQTT:' in lora_family
    assert 'ui_switch_screen(SCREEN_LORA_MQTT)' in home and '"MQTT"' in home
    with tempfile.TemporaryDirectory(prefix="tos-mqtt-") as temporary:
        executable = Path(temporary) / "test"
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                   "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                   "-fno-sanitize-recover=all", "-pthread", "-I", str(ROOT / "tools/crypto/fixtures"), "-I", str(FIXTURES)]
        for directory in (P4 / "Service/spi_bridge/include", P4 / "Drivers/sys_prio/include",
                          P4 / "Applications/LoRa/meshtastic/include",
                          P4 / "Applications/LoRa/session/include", C5 / "Service/mqtt_bridge/include"):
            command += ["-I", str(directory)]
        command += [str(C5 / "Service/mqtt_bridge/mqtt_bridge.c"),
                    str(P4 / "Applications/LoRa/meshtastic/meshtastic_mqtt_codec.c"),
                    str(P4 / "Applications/LoRa/meshtastic/meshtastic_mqtt.c"),
                    str(ROOT / "tools/mqtt/test_mqtt.c"), str(ROOT / "tools/mqtt/test_codec.c"), "-o", str(executable)]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=60)


if __name__ == "__main__":
    main()
