#!/usr/bin/env python3
"""Compile the native update screen against explicit LVGL/task/OTA fixtures."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent


def main():
    with tempfile.TemporaryDirectory(prefix='update-ui-') as temporary:
        work=Path(temporary)
        (work/'esp_err.h').write_text('#include "test_platform.h"\n')
        for header in ('system_update_ui.h','esp_log.h','freertos/FreeRTOS.h','freertos/task.h',
                       'sys_prio.h','ui_chrome.h','ui_manager.h','ui_theme.h'):
            path=work/header;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('#include "test_ui_platform.h"\n')
        command=[*shlex.split(os.environ.get('CC','cc')),'-std=gnu11','-Wall','-Wextra','-Werror',
                 '-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all']
        for path in [work,HERE,HERE/'ui_fixtures',ROOT/'firmware_p4/components/Service/ota/include',
                     ROOT/'firmware_p4/components/Applications/ui/screens/settings']:
            command+=['-I',str(path)]
        binary=work/'test'
        subprocess.run([*command,str(HERE/'test_update_ui.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)


if __name__=='__main__':main()
