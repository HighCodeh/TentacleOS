#!/usr/bin/env python3
"""Production P4 OTA and C5 confirmation against explicit file/flash/OS fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
P4=ROOT/'firmware_p4/components/Service/ota'


def extract(source, signature):
    start=source.index(signature);cursor=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[cursor]=='{')-(source[cursor]=='}');cursor+=1
    return source[start:cursor]


def main():
    with tempfile.TemporaryDirectory(prefix='ota-test-') as temporary:
        work=Path(temporary)
        for header in ('freertos/FreeRTOS.h','freertos/task.h','esp_log.h','esp_ota_ops.h',
                       'esp_system.h','ota_version.h','sd_card_init.h','storage_assets.h',
                       'ui_liveness.h','power_manager.h','esp_err.h'):
            path=work/header;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('#include "test_platform.h"\n')
        command=[*shlex.split(os.environ.get('CC','cc')),'-std=gnu11','-Wall','-Wextra','-Werror',
                 '-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                 '-pthread','-I',str(work),'-I',str(HERE),'-I',str(P4),'-I',str(P4/'include'),
                 '-I',str(ROOT/'firmware_p4/vendor/cjson/cJSON')]
        binary=work/'p4'
        subprocess.run([*command,str(HERE/'test_p4.c'),str(ROOT/'firmware_p4/vendor/cjson/cJSON/cJSON.c'),
                        '-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)
        c5=(ROOT/'firmware_c5/components/Service/ota/ota_service.c').read_text()
        harness=(HERE/'test_c5.c').read_text().replace('/* CONFIRMATION */',extract(c5,'esp_err_t ota_post_boot_check('))
        (work/'c5.c').write_text(harness)
        subprocess.run([*command,str(work/'c5.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)



if __name__=='__main__':main()
