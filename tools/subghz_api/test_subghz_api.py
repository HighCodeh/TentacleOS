#!/usr/bin/env python3
"""Compile native Sub-GHz ownership, bounded backend and spectrum against explicit fakes."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
SERVICE=ROOT/'firmware_p4/components/Service'
DRIVER=ROOT/'firmware_p4/components/Drivers/cc1101'
APPS=ROOT/'firmware_p4/components/Applications/SubGhz'
HERE=Path(__file__).resolve().parent


def main():
    with tempfile.TemporaryDirectory(prefix='subghz-api-') as temp:
        temp=Path(temp)
        (temp/'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
''')
        for name in ['esp_log.h','esp_heap_caps.h','esp_timer.h','driver/rmt_rx.h','driver/rmt_tx.h',
                     'driver/rmt_encoder.h','freertos/FreeRTOS.h','freertos/task.h',
                     'freertos/queue.h','freertos/semphr.h','pin_def.h','sys_prio.h','rom/ets_sys.h','led_control.h']:
            p=temp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "test_platform.h"\n')
        (temp/'led_control.h').write_text('void led_signal_error(void);\n')
        (temp/'power_manager.h').write_text('#include "esp_err.h"\nesp_err_t power_manager_no_sleep_acquire(void);\nesp_err_t power_manager_no_sleep_release(void);\n')
        command=[*shlex.split(os.environ.get('CC','cc')),'-std=gnu11','-Wall','-Wextra','-Werror',
                 '-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread']
        for folder in [temp,HERE/'fixtures',SERVICE/'app_runtime',SERVICE/'app_runtime/include',
                       SERVICE/'host_link/include',ROOT/'firmware_p4/components/Core/resource_mgr/include',
                       DRIVER/'include',APPS,APPS/'include',APPS/'protocols/include']:
            command+=['-I',str(folder)]
        binary=temp/'test'
        subprocess.run([*command,str(HERE/'test_backend.c'),str(DRIVER/'cc1101_lease.c'),
                        '-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)
        source=(DRIVER/'cc1101.c').read_text()
        start=source.index('static void write_burst_impl(')
        end=source.index('static long linear_map',start)
        wrappers=source[start:end]
        signatures=list(re.finditer(r'^static (void|uint8_t|bool|esp_err_t) (\w+_impl)\(([^)]*)\);',wrappers,re.M))
        fake_impl=[];foreign_calls=[];native_calls=[]
        for match in signatures:
            ret,name,params=match.groups()
            fallback={'void':'','uint8_t':'return 42;','bool':'return true;','esp_err_t':'return ESP_OK;'}[ret]
            fake_impl.append(f'static {ret} {name}({params}) {{ ++s_calls; if(s_check_inflight) assert(cc1101_lease_acquire()==ESP_ERR_INVALID_STATE); {fallback} }}')
            args=[]
            if params!='void':
                for part in params.split(','):
                    arg=re.search(r'(\w+)\s*$',part).group(1)
                    args.append('buffer' if arg in ['buf','data'] else '3')
            call='cc1101_'+name[:-5]+'('+','.join(args)+')'
            foreign_calls.append(call+';' if ret=='void' else 'assert('+call+'=='+('false' if ret=='bool' else 'ESP_ERR_INVALID_STATE' if ret=='esp_err_t' else '0')+');')
            native_calls.append(call+';')
        driver_test=(HERE/'test_lease.c').read_text().replace('/* WRAPPERS */',wrappers+'\n'+'\n'.join(fake_impl))
        driver_test=driver_test.replace('/* FOREIGN_CALLS */','\n'.join(foreign_calls)).replace('/* NATIVE_CALLS */','\n'.join(native_calls))
        def function(signature):
            start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
            while depth:
                depth+=(source[end]=='{')-(source[end]=='}');end+=1
            return source[start:end]
        driver_test=driver_test.replace('/* TRANSACTIONS */', function('static esp_err_t cc1101_txn(')+'\n'+function('static esp_err_t cc1101_reset_radio('))
        driver_test=driver_test.replace('/* COUNT */',str(len(signatures)))
        (temp/'lease_test.c').write_text(driver_test)
        subprocess.run([*command,str(temp/'lease_test.c'),str(DRIVER/'cc1101_lease.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)
        subprocess.run([*command,str(HERE/'test_native_tx.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)
        subprocess.run([*command,str(HERE/'test_spectrum.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)


if __name__=='__main__':main()
