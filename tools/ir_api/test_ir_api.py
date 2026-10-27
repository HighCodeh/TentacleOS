#!/usr/bin/env python3
"""Test production IR leasing, lifecycle wrappers, local worker backend and ABI."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
SERVICE=ROOT/'firmware_p4/components/Service'

def function(source, signature):
    start=source.index(signature)
    opening=source.index('{',start)
    depth,end=1,opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[start:end]

def main():
    driver=(SERVICE/'ir/ir.c').read_text()
    start=driver.index('static bool s_has_torch;')
    end=driver.index('static esp_err_t rx_init_impl(void) {')
    harness=(ROOT/'tools/ir_api/test_ir_api.c').read_text()
    harness=harness.replace('/* PRODUCTION_WRAPPERS */',driver[start:end])
    for kind in ('RX','TX'):
        harness=harness.replace('/* PRODUCTION_'+kind+'_DEINIT */',function(driver,'static void '+kind.lower()+'_deinit_impl(void) {'))
    with tempfile.TemporaryDirectory(prefix='tos-ir-api-') as tmp:
        tmp=Path(tmp);(tmp/'test.c').write_text(harness)
        production=(SERVICE/'app_runtime/tos_api_ir.c').read_text()
        production='#include <stdio.h>\n#include <stdlib.h>\nFILE *ir_test_open(const char *,const char *);\nvoid *ir_test_malloc(size_t);\nvoid *ir_test_calloc(size_t,size_t);\n'+production
        production=production.replace('fopen(path,', 'ir_test_open(path,').replace('calloc(count,', 'ir_test_calloc(count,').replace('content = malloc(', 'content = ir_test_malloc(')
        (tmp/'tos_api_ir.c').write_text(production)
        command=[*shlex.split(os.environ.get('CC','cc')),'-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread']
        for folder in (ROOT/'tools/ir_api/fixtures',ROOT/'tools/mqtt/fixtures',SERVICE/'ir/include',
                       SERVICE/'host_link/include',SERVICE/'app_runtime/include',SERVICE/'spi_bridge/include'):
            command+=['-I',str(folder)]
        command += [str(tmp/'test.c'),str(SERVICE/'ir/ir_lease.c'),str(SERVICE/'host_link/host_link_local_ir.c'),
                    str(tmp/'tos_api_ir.c'),str(SERVICE/'ir/ir_file.c'),'-o',str(tmp/'test')]
        subprocess.run(command,check=True,timeout=60)
        subprocess.run([str(tmp/'test')],check=True,timeout=30)

if __name__=='__main__':main()
