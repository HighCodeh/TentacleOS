#!/usr/bin/env python3
"""Exercise actual power-manager policy with explicit PM/RTOS fakes, PM on and off."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
MODULE = ROOT / 'firmware_p4/components/Service/power_manager'


def main():
    with tempfile.TemporaryDirectory(prefix='power-policy-') as temporary:
        work = Path(temporary)
        for header in ('esp_err.h', 'esp_log.h', 'esp_pm.h', 'sdkconfig.h', 'freertos/FreeRTOS.h'):
            path = work / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "test_platform.h"\n')
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(work), '-I', str(HERE), '-I', str(MODULE), '-I', str(MODULE / 'include')]
        for enabled in (0, 1):
            binary = work / 'test'
            subprocess.run([*command, f'-DCONFIG_PM_ENABLE={enabled}', str(HERE / 'test_power_policy.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
