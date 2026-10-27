#!/usr/bin/env python3
"""Inject a bus failure at every operation of the real NFC target startup."""
from pathlib import Path
import os
import re
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
    source = (ROOT/'firmware_p4/components/Applications/nfc/protocols/mifare/mf_classic_emu.c').read_text()
    functions = '\n'.join(function(source, signature) for signature in (
        'static hb_nfc_err_t target_enter_listen(void) {',
        'hb_nfc_err_t mfc_emu_configure_target(void) {', 'hb_nfc_err_t mfc_emu_start(void) {'))
    constants = '\n'.join(re.findall(r'^#define (?:MFC_|IC_IDENTITY_)[^\n]+', source, re.M))
    fixture = Path(__file__).with_name('test_startup.c').read_text()
    fixture = fixture.replace('/* CONSTANTS */', constants).replace('/* STARTUP */', functions)
    with tempfile.TemporaryDirectory(prefix='tos-nfc-startup-') as tmp:
        directory = Path(tmp)
        (directory/'test.c').write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-std=c11','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                        '-I',str(ROOT/'firmware_p4/components/Drivers/st25r3916/include'),
                        str(directory/'test.c'),'-o',str(directory/'test')],check=True,timeout=60)
        subprocess.run([str(directory/'test')],check=True,timeout=30)


if __name__ == '__main__':
    main()
