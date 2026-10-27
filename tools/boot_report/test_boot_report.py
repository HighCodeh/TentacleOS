#!/usr/bin/env python3
"""Fault-inject the real boot-report module; no flash, NVS partition or device access."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
MODULE = ROOT / 'firmware_p4/components/Service/boot_report'


def main():
    with tempfile.TemporaryDirectory(prefix='boot-report-') as directory:
        work = Path(directory)
        for header in ('esp_err.h', 'esp_system.h', 'esp_attr.h', 'esp_core_dump.h',
                       'esp_log.h', 'esp_timer.h', 'nvs.h'):
            (work / header).write_text('#include "test_platform.h"\n')
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-pthread', '-fsanitize=address,undefined',
                   '-fno-sanitize-recover=all', '-I', str(work), '-I', str(HERE),
                   '-I', str(MODULE), '-I', str(MODULE / 'include')]
        for coredump in (0, 1):
            binary = work / 'test'
            subprocess.run([*command, f'-DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH={coredump}',
                            f'-DCONFIG_ESP_COREDUMP_DATA_FORMAT_ELF={coredump}',
                            str(HERE / 'test_boot_report.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)

    subprocess.run([os.sys.executable, str(HERE/'test_operations.py')], check=True, timeout=120)


if __name__ == '__main__':
    main()
