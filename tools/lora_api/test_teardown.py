#!/usr/bin/env python3
"""Test actual driver/worker stop and stream-barrier functions under delayed acknowledgements."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    driver = (ROOT/'firmware_p4/components/Drivers/sx1262/sx1262.c').read_text()
    bridge = (ROOT/'firmware_p4/components/Service/spi_bridge/spi_bridge.c').read_text()
    native = '\n'.join(function(driver, signature) for signature in (
        'void sx1262_stop(void) {', 'esp_err_t sx1262_deinit(void) {'))
    native += '\n' + function(bridge, 'esp_err_t spi_bridge_stream_barrier(')
    fixture = (Path(__file__).with_name('test_teardown.c')).read_text()
    fixture = fixture.replace('/* DRIVER */', native)
    for name in ('meshtastic', 'meshcore'):
        source = (ROOT/f'firmware_p4/components/Applications/LoRa/{name}/{name}_app.c').read_text()
        body = function(source, f'esp_err_t {name}_app_stop(void) {{')
        fixture = fixture.replace(f'/* {name.upper()} */', body)
    bridges = []
    for name, short in (('meshtastic','mt'), ('meshcore','mc')):
        source = (ROOT/f'firmware_p4/components/Applications/LoRa/{name}/{name}_phone_bridge.c').read_text()
        body = function(source, f'esp_err_t {name}_phone_bridge_stop(void) {{')
        for old, new in ((f'{name}_phone_bridge_stop', f'actual_{short}_stop'),
                         ('s_is_initialized','s_bridge_initialized'), ('s_is_running','s_bridge_running'),
                         ('s_notify_task','s_bridge_task'), ('s_status_task','s_bridge_task'),
                         ('s_prev_vprintf','s_saved_log')):
            body = body.replace(old,new)
        bridges.append(body)
    fixture = fixture.replace('/* BRIDGES */','\n'.join(bridges))
    mesh = (ROOT/'firmware_p4/components/Applications/LoRa/meshtastic/meshtastic_mesh.c').read_text()
    fixture = fixture.replace('/* MESH */', function(mesh, 'esp_err_t meshtastic_mesh_stop(void) {'))
    with tempfile.TemporaryDirectory(prefix='tos-lora-teardown-') as tmp:
        directory = Path(tmp)
        (directory/'test.c').write_text(fixture)
        command = [*shlex.split(os.environ.get('CC','cc')), '-std=c11','-Wall','-Wextra','-Werror',
                   '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                   str(directory/'test.c'),'-o',str(directory/'test')]
        subprocess.run(command,check=True,timeout=60)
        subprocess.run([str(directory/'test')],check=True,timeout=30)


if __name__ == '__main__':
    main()
