#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Ultralight AES auth and MAC state tested with NXP and independent vectors."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from test_emu_tx import extract
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.cmac import CMAC
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc/protocols/t2t"

def main():
    key=bytes(16)
    b=bytes.fromhex('1AE4174CA173EBBC59165CEBE2F20821')
    a=bytes.fromhex('F29B0123F5C00DF612487BBF42468C7E')
    def encrypt(data):
        return Cipher(algorithms.AES(key),modes.CBC(bytes(16))).encryptor().update(data)
    challenge=bytes.fromhex('AFD5A847B84862FF3874A7F07B8DDF351B')
    continuation=bytes.fromhex('AFCDF22C5F7A92F0AF0155612B9B236AC7A424BC5238D41AD041B8165B7D99E524')
    reply=bytes.fromhex('002C743D6B1E128F8076BD197B76012CE8')
    assert challenge == b'\xaf'+encrypt(b)
    assert continuation == b'\xaf'+encrypt(a+b[1:]+b[:1])
    assert reply == b'\x00'+encrypt(a[1:]+a[:1])
    vector=bytes.fromhex('5AA500010080')+a[:2]+bytes(x^y for x,y in zip(a[2:8],b[:6]))+b[6:]+a[8:]
    c=CMAC(algorithms.AES(key));c.update(vector);session_key=c.finalize()
    def mac(counter,data):
        c=CMAC(algorithms.AES(session_key));c.update(counter.to_bytes(2,'little')+data)
        return c.finalize()[1::2]
    values=dict(key=key,reader_nonce=a,nonce=b,challenge=challenge,continuation=continuation,reply=reply,
                session_key=session_key,command=b'\x30\x04'+mac(0,b'\x30\x04'),
                response_mac=mac(1,bytes(range(16))),ack_mac=mac(3,b''),
                last_command=b'\x30\x04'+mac(65534,b'\x30\x04'),last_mac=mac(65535,b''),
                write_command=b'\xa2\x04abcd'+mac(2,b'\xa2\x04abcd'))
    arrays='\n'.join('static const uint8_t '+name+'[]={'+','.join(map(str,data))+'};' for name,data in values.items())
    header=re.sub(r'^#include.*$','',(BASE/'include/t2t_aes.h').read_text(),flags=re.M)
    source=re.sub(r'^#include.*$','',(BASE/'t2t_aes.c').read_text(),flags=re.M)
    engine=(BASE/'t2t_emu.c').read_text()
    handler=extract(engine[engine.rindex('static void handle_aes_auth('):], 'static void handle_aes_auth(')
    constants='\n'.join(line for line in (engine+(BASE/'include/t2t_emu.h').read_text()).splitlines() if line.startswith('#define T2T_'))
    poller=(BASE.parent/'mifare/mf_ultralight.c').read_text()
    poller_code='\n'.join(line for line in poller.splitlines() if line.startswith(('#define MFUL_', '#define MF_ACK_')))
    for signature in ['bool mful_is_aes(', 'esp_err_t mful_aes_auth(', 'static bool aes_exchange(', 'static int aes_read_memory(', 'int mful_aes_read_all(', 'esp_err_t mful_aes_read_card(', 'esp_err_t mful_aes_write_page(']:
        poller_code += '\n' + extract(poller, signature)
    service=(BASE.parent.parent/'nfc_protocol_service.c').read_text()
    key_parser=extract(service,'hb_nfc_err_t nfc_protocol_service_ul_aes_key(')
    types=(BASE/'include/t2t_emu.h').read_text()
    types=types[types.index('typedef enum {'):types.index('} t2t_emu_config_t;')+len('} t2t_emu_config_t;')]
    fixture=Path(__file__).with_suffix('.c').read_text().replace('/* HEADER */',header).replace('/* PRODUCTION */',source).replace('/* VECTORS */',arrays).replace('/* ENGINE DEFINES */',constants).replace('/* ENGINE */',handler).replace('/* POLLER */',poller_code).replace('/* KEY PARSER */',key_parser).replace('/* CARD TYPES */',types)
    with tempfile.TemporaryDirectory(prefix='nfc-aes-') as tmp:
        p=Path(tmp);(p/'test.c').write_text(fixture)
        openssl=Path(os.environ.get('OPENSSL_PREFIX','/opt/homebrew/opt/openssl@3'))
        flags=['-I'+str(openssl/'include'),'-L'+str(openssl/'lib')] if openssl.exists() else []
        subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',
                        '-Wno-deprecated-declarations','-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all',*flags,str(p/'test.c'),'-lcrypto','-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':
    main()
