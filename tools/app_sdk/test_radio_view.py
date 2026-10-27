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

"""Test real radio adapters and native chat-ring synchronization."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / 'firmware_p4/components/Service'
APPS = ROOT / 'firmware_p4/components/Applications'


def main():
    with tempfile.TemporaryDirectory(prefix='radio-view-') as temporary:
        work = Path(temporary)
        (work / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
        (work / 'esp_log.h').write_text('#define ESP_LOGD(tag, ...) ((void)(tag))\n')
        (work / 'cc1101.h').write_text('#pragma once\ntypedef int cc1101_preset_t;\n')
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11',
                   '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra',
                   '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(work), '-I', str(SERVICE / 'app_runtime/include'),
                   '-I', str(SERVICE / 'host_link/include'),
                   '-I', str(SERVICE / 'spi_bridge/include'),
                   '-I', str(APPS / 'LoRa/session/include'), '-I', str(APPS / 'SubGhz/include')]
        binary = work / 'test'
        subprocess.run([*command, str(SERVICE / 'app_runtime/tos_api_radio_view.c'),
                        str(ROOT / 'tools/app_sdk/tests/test_radio_view.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)

        source = (APPS / 'LoRa/session/lora_session.c').read_text()
        prefix = source[:source.index('static esp_err_t start_protocol(')]
        prefix = '\n'.join(line for line in prefix.splitlines() if not line.startswith('#include') and not line.startswith('static esp_err_t stop_protocol('))
        def function(signature):
            start = source.index(signature)
            opening = source.index('{', start)
            depth, end = 1, opening + 1
            while depth:
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            return source[start:end]
        test = (ROOT / 'tools/app_sdk/tests/test_chat_ring.c').read_text()
        native = prefix + '\n' + '\n'.join(function(signature) for signature in (
            'uint16_t lora_session_msg_count(', 'uint16_t lora_session_msg_since(',
            'void lora_session_on_rx_text('))
        (work / 'ring.c').write_text(test.replace('/* NATIVE_RING */', native))
        subprocess.run([*command, '-pthread', '-Wno-unused-variable', str(work / 'ring.c'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)

        tx = (ROOT / 'tools/app_sdk/tests/test_chat_tx.c').read_text()
        native_tx = prefix + '\nstatic esp_err_t stop_protocol(lora_proto_t proto);\n' + '\n'.join(function(signature) for signature in (
            'static esp_err_t start_protocol(', 'static esp_err_t start_session(',
            'esp_err_t lora_session_start(', 'esp_err_t lora_session_start_owned(',
            'esp_err_t lora_session_radio_reserve(', 'esp_err_t lora_session_radio_release(',
            'static esp_err_t stop_session(', 'esp_err_t lora_session_stop(',
            'esp_err_t lora_session_stop_owned(', 'static esp_err_t stop_protocol(lora_proto_t proto) {',
            'lora_proto_t lora_session_active(', 'static esp_err_t send_text(',
            'esp_err_t lora_session_send_text(', 'void lora_session_process_tx('))
        (work / 'tx.c').write_text(tx.replace('/* NATIVE_TX */', native_tx))
        subprocess.run([*command, '-pthread', '-Wno-unused-variable', str(work / 'tx.c'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)

    subprocess.run([sys.executable, str(ROOT / "tools/lora_api/test_sessions.py")], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools/lora_api/test_teardown.py")], check=True)

if __name__ == '__main__':
    main()
