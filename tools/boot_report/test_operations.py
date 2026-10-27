#!/usr/bin/env python3
"""Exercise production monitor/boot/audio code with explicit scheduler and HAL fakes."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def compile_run(directory, name, fixture, source, extra=()):
    source = re.sub(r'^#include "[^"\n]+"\s*$', '', source, flags=re.M)
    test = directory / f'{name}.c'
    test.write_text(fixture.replace('/* MODULE */', source))
    binary = directory / name
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *extra, str(test), '-o', str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30, cwd=directory)


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    with tempfile.TemporaryDirectory(prefix='tos-operations-') as temporary:
        directory = Path(temporary)
        for chip in ('p4', 'c5'):
            compile_run(directory, chip, (HERE/'test_monitor.c').read_text(),
                        (ROOT/f'firmware_{chip}/components/Core/sys_monitor.c').read_text(),
                        ('-DTEST_P4',) if chip == 'p4' else ())

        compile_run(directory, 'entry', (HERE/'test_main.c').read_text(),
                    (ROOT/'firmware_p4/main/main.c').read_text())
        compile_run(directory, 'kernel', (HERE/'test_kernel.c').read_text(),
                    (ROOT/'firmware_p4/components/Core/kernel.c').read_text())
        compile_run(directory, 'sound', (HERE/'test_boot_sound.c').read_text(),
                    (ROOT/'firmware_p4/components/Service/boot_sound/boot_sound.c').read_text())
        reset = (ROOT/'firmware_p4/components/Service/storage_api/tos_factory_reset.c').read_text()
        names = sorted(set(re.findall(r'TOS_PATH_[A-Z_]+', reset)))
        paths = '\n'.join(f'#define {name} "sd/{name.lower().removeprefix("tos_path_")}"' for name in names)
        paths = paths.replace('"sd/config_dir"', '"sd/config"')
        compile_run(directory, 'factory', (HERE/'test_factory_reset.c').read_text().replace('/* PATHS */', paths), reset)
        screen = (ROOT/'firmware_p4/components/Applications/ui/screens/safe_mode/safe_mode_ui.c').read_text()
        compile_run(directory, 'recovery', (HERE/'test_recovery_ui.c').read_text(),
                    '\n'.join(function(screen, name) for name in (
                        'static void show_reset_error(', 'static void factory_task(',
                        'static void start_reset(')))
        audio = (ROOT/'firmware_p4/components/Drivers/audio_i2s/audio_i2s.c').read_text()
        signatures = ('static esp_err_t tx_drain(', 'static esp_err_t tx_set_rate(',
                      'static esp_err_t tx_begin(', 'static esp_err_t tx_end(',
                      'static int write_all(', 'static void write_silence(',
                      'esp_err_t audio_i2s_play_pcm(', 'esp_err_t audio_i2s_stream_start(',
                      'int audio_i2s_stream_write(', 'void audio_i2s_stream_stop(')
        constants = '\n'.join(re.findall(r'^#define [^\n]+', audio, re.M))
        compile_run(directory, 'audio', (HERE/'test_audio.c').read_text().replace('/* CONSTANTS */', constants),
                    '\n'.join(function(audio, signature) for signature in signatures))


if __name__ == '__main__':
    main()
