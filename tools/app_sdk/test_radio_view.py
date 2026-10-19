#!/usr/bin/env python3
"""Test real read-only radio adapters and native chat-ring synchronization."""
import os
from pathlib import Path
import shlex
import subprocess
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
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
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
        prefix = source[:source.index('esp_err_t lora_session_start(')]
        prefix = '\n'.join(line for line in prefix.splitlines() if not line.startswith('#include'))
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


if __name__ == '__main__':
    main()
