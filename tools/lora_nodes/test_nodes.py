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

"""Exercise actual node databases/session copies and MeshCore receive boundaries with explicit fakes."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
LORA = ROOT / 'firmware_p4/components/Applications/LoRa'

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

def main():
    with tempfile.TemporaryDirectory(prefix='lora-nodes-') as temporary:
        work = Path(temporary)
        (work/'esp_err.h').write_text('#pragma once\n#include "tos_api.h"\nstatic inline const char *esp_err_to_name(esp_err_t error) {(void)error;return "fake";}\n')
        (work/'esp_attr.h').write_text('#define EXT_RAM_BSS_ATTR\n')
        (work/'esp_log.h').write_text('''#pragma once
static inline void test_log(const char *format, ...) {(void)format;}
#define ESP_LOGD(tag, ...) ((void)(tag), test_log(__VA_ARGS__))
#define ESP_LOGI(tag, ...) ((void)(tag), test_log(__VA_ARGS__))
#define ESP_LOGW(tag, ...) ((void)(tag), test_log(__VA_ARGS__))
#define ESP_LOGE(tag, ...) ((void)(tag), test_log(__VA_ARGS__))
''')
        (work/'freertos').mkdir()
        (work/'freertos/FreeRTOS.h').write_text('#pragma once\n#include <stdint.h>\n#define portTICK_PERIOD_MS 1\n#define EXT_RAM_BSS_ATTR\n')
        (work/'freertos/task.h').write_text('uint32_t xTaskGetTickCount(void);\n')
        session = (LORA/'session/lora_session.c').read_text()
        prefix = session[:session.index('static esp_err_t start_protocol(')]
        prefix = '\n'.join(line for line in prefix.splitlines() if not line.startswith('#include'))
        native = prefix + '\n' + function(session, 'esp_err_t lora_session_nodes_snapshot(')
        router = (LORA/'meshcore/meshcore_router.c').read_text()
        constants = '\n'.join(line for line in router.splitlines() if line.startswith('#define MC_'))
        receive = constants + '\n' + '\n'.join(function(router, signature) for signature in (
            'static void process_grp_txt(const meshcore_packet_view_t *pkt) {',
            'static void process_txt_msg(const meshcore_packet_view_t *pkt) {',
            'static void process_path_payload(const meshcore_packet_view_t *pkt) {'))
        test = (HERE/'test_nodes.c').read_text().replace('/* SESSION */', native).replace('/* RECEIVE */', receive)
        (work/'test.c').write_text(test)
        command = [*shlex.split(os.environ.get('CC','cc')), '-std=gnu11', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-pthread',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        for folder in [work, ROOT/'firmware_p4/components/Service/app_runtime/include',
                       ROOT/'firmware_p4/components/Drivers/sx1262/include', LORA/'session/include', LORA/'meshtastic/include', LORA/'meshcore/include']:
            command += ['-I', str(folder)]
        binary = work/'test'
        subprocess.run([*command,str(work/'test.c'),str(LORA/'meshtastic/meshtastic_nodedb.c'),
                        str(LORA/'meshcore/meshcore_db.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=60)

if __name__ == '__main__':
    main()
