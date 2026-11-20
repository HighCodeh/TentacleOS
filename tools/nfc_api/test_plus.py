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

"""Check the production Plus poller against independently generated AES/CMAC traces.

Requires Python cryptography and OpenSSL development headers/library.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.cmac import CMAC
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc/protocols/mifare"

def main():
    vectors = []
    def add(name, value):
        vectors.append("static const uint8_t " + name + "[] = {" + ",".join(map(str,value)) + "};")
        return value
    key = add("key", bytes(range(16)))
    a = add("rnd_a", bytes(range(16,32)))
    b = add("rnd_b", bytes(range(32,48)))
    ti = add("ti", bytes.fromhex("11223344"))
    data = add("data", bytes(range(64,80)))
    def enc(key, data, iv=bytes(16)):
        return Cipher(algorithms.AES(key),modes.CBC(iv)).encryptor().update(data)
    ke = add("k_enc",enc(key,a[11:]+b[11:]+bytes(x^y for x,y in zip(a[4:9],b[4:9]))+b"\x11"))
    km = add("k_mac",enc(key,a[7:12]+b[7:12]+bytes(x^y for x,y in zip(a[:5],b[:5]))+b"\x22"))
    def mac(op,ctr,payload):
        c=CMAC(algorithms.AES(km));c.update(bytes([op])+ctr.to_bytes(2,"little")+ti+payload)
        return c.finalize()[1::2]
    add("auth_first",b"\x70\x04\x40\x00")
    add("auth_nonfirst",b"\x76\x04\x40")
    add("challenge",b"\x90"+enc(key,b))
    add("auth_continue",b"\x72"+enc(key,a+b[1:]+b[:1]))
    add("auth_reply",b"\x90"+enc(key,ti+a[1:]+a[:1]+bytes(12)))
    add("nonfirst_reply",b"\x90"+enc(key,a[1:]+a[:1]))
    payload=b"\x08\x00\x01"
    add("read_plain",b"\x33"+payload+mac(0x33,0,payload))
    add("plain_reply",b"\x90"+data+mac(0x90,1,payload+data))
    add("read_enc",b"\x31"+payload+mac(0x31,1,payload))
    cipher=enc(ke,data,b"\x02\x00\x00\x00"*3+ti)
    add("enc_reply",b"\x90"+cipher+mac(0x90,2,payload+cipher))
    cipher=enc(ke,data,ti+b"\x02\x00\x00\x00"*3)
    wp=b"\x08\x00"+cipher
    add("write_enc",b"\xa1"+wp+mac(0xa1,0,wp))
    add("write_reply",b"\x90"+mac(0x90,1,b""))
    wp=b"\x08\x00"+data
    add("write_plain",b"\xa3"+wp+mac(0xa3,1,wp))
    add("write_reply2",b"\x90"+mac(0x90,2,b""))
    add("read_bad",b"\x33"+payload+mac(0x33,2,payload))
    add("bad_reply",b"\x90"+data+mac(0x90,3,payload+data))
    payload=b"\x08\x00\x03"
    add("multi_plain",b"\x33"+payload+mac(0x33,0,payload))
    add("multi_plain_reply",b"\x90"+data*3+mac(0x90,1,payload+data*3))
    cipher=enc(ke,data*3,b"\x02\x00\x00\x00"*3+ti)
    add("multi_enc",b"\x31"+payload+mac(0x31,1,payload))
    add("multi_enc_reply",b"\x90"+cipher+mac(0x90,2,payload+cipher))
    source=(BASE/"mf_plus.c").read_text()
    source=re.sub(r'^#include.*$', '', source, flags=re.M)
    header=(BASE/"include/mf_plus.h").read_text()
    header=re.sub(r'^#include.*$', '', header, flags=re.M)
    fixture=Path(__file__).with_suffix('.c').read_text().replace('/* VECTORS */','\n'.join(vectors))
    fixture=fixture.replace('/* HEADER */',header).replace('/* PRODUCTION */',source)
    with tempfile.TemporaryDirectory(prefix='nfc-plus-') as tmp:
        p=Path(tmp);(p/'test.c').write_text(fixture)
        openssl=Path(os.environ.get('OPENSSL_PREFIX','/opt/homebrew/opt/openssl@3'))
        flags=['-I'+str(openssl/'include'),'-L'+str(openssl/'lib')] if openssl.exists() else []
        subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',
                        '-Wno-deprecated-declarations','-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all',*flags,str(p/'test.c'),'-lcrypto','-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':
    main()
