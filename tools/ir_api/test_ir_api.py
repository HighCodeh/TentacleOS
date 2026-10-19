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
        command=[*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread']
        for folder in (ROOT/'tools/ir_api/fixtures',ROOT/'tools/mqtt/fixtures',SERVICE/'ir/include',
                       SERVICE/'host_link/include',SERVICE/'app_runtime/include',SERVICE/'spi_bridge/include'):
            command+=['-I',str(folder)]
        command += [str(tmp/'test.c'),str(SERVICE/'ir/ir_lease.c'),str(SERVICE/'host_link/host_link_local_ir.c'),
                    str(SERVICE/'app_runtime/tos_api_ir.c'),'-o',str(tmp/'test')]
        subprocess.run(command,check=True,timeout=60)
        subprocess.run([str(tmp/'test')],check=True,timeout=30)

if __name__=='__main__':main()
