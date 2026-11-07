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

"""Validate complete NTAG Flipper imports and rejection of malformed dumps."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'firmware_p4/components/Applications/nfc'
HEAD = """Filetype: Flipper NFC device
Version: 4
Device type: NTAG/Ultralight
UID: 04 11 22 33 44 55 66
ATQA: 00 44
SAK: 00
NTAG/Ultralight type: NTAG213
Pages total: 45
Pages read: 45
"""
MEM = bytearray(45 * 4)
MEM[:3] = bytes.fromhex('04 11 22')
MEM[4:8] = bytes.fromhex('33 44 55 66')
HEAD += 'Signature: ' + 'AB ' * 31 + 'AB\nMifare version: 00 04 04 02 01 00 0F 03\nCounter 2: 1193046\n'
VALID = HEAD + ''.join(f'Page {i}: {MEM[i*4:i*4+4].hex(" ")}\n' for i in range(45))
with tempfile.TemporaryDirectory(prefix='nfc-t2t-file-') as tmp:
    p = Path(tmp)
    (p/'esp_log.h').write_text('#define ESP_LOGD(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n')
    (p/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_NOT_FOUND 2\n#define ESP_ERR_INVALID_RESPONSE 3\n#define ESP_ERR_NO_MEM 4\n#define ESP_ERR_INVALID_SIZE 5\n')
    (p/'highboy_nfc_error.h').write_text('typedef int hb_nfc_err_t;\n')
    (p/'highboy_nfc_compat.h').write_text('')
    (p/'storage_atomic.h').write_text('#include <stddef.h>\nint storage_write_atomic(const char *, const void *, size_t);\n')
    (p/'test.c').write_text("""#include <assert.h>
#include "nfc_t2t_file.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
int storage_write_atomic(const char *path,const void *data,size_t length) {
  if(strstr(path,"failure")) return 99;
  FILE *f=fopen(path,"wb"); if(!f) return 99;
  int ok=fwrite(data,1,length,f)==length;
  return fclose(f)==0 && ok ? 0 : 99;
}
int main(int argc,char **argv) {
  nfc_t2t_file_t card;
  for(int i=1;i<argc;i++) {
    int err=nfc_t2t_file_load(argv[i],&card);
    if(i==1) { assert(err==0); assert(card.length==240 || card.length==192 || card.length==64 || card.length==80 || card.length==164 || card.length==180 || card.length==540 || card.length==924 || card.length==944 || card.length==1940 || card.length==1968); assert(card.config.uid_len==7); assert(card.config.has_signature); assert(card.config.signature[31]==0xAB); assert(card.config.has_version); assert(card.config.has_counter && card.config.counter==1193046); }
    if(i==1) {
      char saved[4096];snprintf(saved,sizeof(saved),"%s.saved",argv[i]);
      card.config.pwd_known=true;card.config.pack_known=false;
      card.config.failed_auth_attempts=123;
      card.config.aes_key_known[0]=0xFFFF;card.config.aes_key_known[1]=0x000F;
      card.config.aes_signature_lock=T2T_SIGNATURE_PERMANENT;
      card.config.has_aes_signature=true;memset(card.config.aes_signature,0xBC,48);
      assert(nfc_t2t_file_save(saved,&card)==0);
      nfc_t2t_file_t roundtrip;
      assert(nfc_t2t_file_load(saved,&roundtrip)==0);
      assert(roundtrip.length==card.length);
      assert(!memcmp(roundtrip.memory,card.memory,card.length));
      assert(roundtrip.config.pwd_known && !roundtrip.config.pack_known && !roundtrip.config.enable_pwd);
      assert(roundtrip.config.failed_auth_attempts==123);
      if(card.config.type==T2T_TAG_ULTRALIGHT_AES) {
        assert(roundtrip.config.aes_key_known[0]==0xFFFF && roundtrip.config.aes_key_known[1]==0x000F);
        assert(roundtrip.config.has_aes_signature && roundtrip.config.aes_signature[47]==0xBC);
        assert(roundtrip.config.aes_signature_known==T2T_AES_SIGNATURE_KNOWN);
        assert(roundtrip.config.aes_signature_lock==T2T_SIGNATURE_PERMANENT);
        card.config.has_aes_signature=false;card.config.aes_signature_known=0x801;
        assert(nfc_t2t_file_save(saved,&card)==0);
        assert(nfc_t2t_file_load(saved,&roundtrip)==0);
        assert(!roundtrip.config.has_aes_signature && roundtrip.config.aes_signature_known==0x801);
        assert(roundtrip.config.aes_signature[47]==0xBC);
        card.config.aes_signature_known=0x1000;
        assert(nfc_t2t_file_save(saved,&card)==ESP_ERR_INVALID_ARG);
        card.config.aes_signature_known=0;

      }
      assert(roundtrip.config.counter==card.config.counter);
      assert(roundtrip.config.enable_ulc_auth==card.config.enable_ulc_auth);
      assert(nfc_t2t_file_save("failure",&card)==99);
      card.length--;assert(nfc_t2t_file_save(saved,&card)!=0);card.length++;
    }
    if(i==1 && card.length==192) {
      assert(card.config.type==T2T_TAG_ULTRALIGHT_C);
      assert(card.config.enable_ulc_auth == (strstr(argv[i],"unknown-key") == NULL));
      if(card.config.enable_ulc_auth) assert(card.config.ulc_key[15]==15);
    }
    else if(i!=1) { assert(err!=0); assert(card.length==0); }
  }
}
""")
    subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I'+str(p), '-I'+str(BASE/'include'), '-I'+str(BASE/'protocols/t2t/include'), str(BASE/'nfc_t2t_file.c'), str(BASE/'nfc_t2t_save.c'),
                    str(p/'test.c'), '-o', str(p/'test')], check=True)
    cases=[VALID, VALID+'UID: 04 11 22 33 44 55 66\n',
           VALID.replace('Pages read: 45','Pages read: 44'),
           VALID.replace('Page 44:', 'Page 43:'),
           VALID.replace('Page 44:', 'Page 231:'),
           VALID.replace('Page 0: 04','Page 0: ??'),
           VALID.replace('NTAG213','NTAG215'),
           VALID.replace('Page 0: 04','Page 0: 05'),
           VALID.replace('Pages total: 45','Pages total: 99999999999999999999'),
           VALID.replace('SAK: 00','SAK: 08'),
           VALID.replace('Version: 4','Version: 5'),
           VALID.replace('Page 1:', 'Page -1:'),
           VALID + 'Counter 2: 1\n', VALID.replace('Counter 2: 1193046','Counter 2: 16777216'),
           VALID + 'Signature: 00\n', VALID.replace('Mifare version: 00','Mifare version: ??'),
           VALID.replace('Counter 2: 1193046','Counter 2: -1')]
    paths=[]
    for i,content in enumerate(cases):
        path=p/f'{i}.nfc';path.write_text(content);paths.append(str(path))
    subprocess.run([str(p/'test'),*paths],check=True)
    for name, count in [('NTAG I2C 1K',231), ('NTAG I2C 2K',485), ('NTAG I2C Plus 1K',236), ('NTAG I2C Plus 2K',492), ('Mifare Ultralight AES',60), ('NTAG210', 20), ('NTAG212', 41), ('Mifare Ultralight C', 48), ('Mifare Ultralight', 16), ('Mifare Ultralight 11', 20),
                        ('Mifare Ultralight 21', 41), ('NTAG215', 135), ('NTAG216', 231)]:
        head = HEAD.replace('NTAG213',name).replace('Pages total: 45',f'Pages total: {count}').replace('Pages read: 45',f'Pages read: {count}')
        if name.startswith('NTAG I2C'):
            minor = 2 if 'Plus' in name else 1
            size = 0x15 if name.endswith('2K') else 0x13
            head = head.replace('00 04 04 02 01 00 0F 03',f'00 04 04 05 02 {minor:02X} {size:02X} 03')
        memory = bytearray(count*4)
        memory[:min(len(memory),len(MEM))] = MEM[:len(memory)]
        if name == 'Mifare Ultralight C':
            memory[176:192] = bytes(range(16))
        contents = head + ''.join(f'Page {i}: {memory[i*4:i*4+4].hex(" ")}\n' for i in range(count))
        path = p/'variant.nfc';path.write_text(contents)
        subprocess.run([str(p/'test'),str(path)],check=True)
        if name.startswith('NTAG I2C'):
            invalid = p/'wrong-version.nfc'
            invalid.write_text(contents.replace('00 04 04 05 02','00 04 04 02 01'))
            subprocess.run([str(p/'test'),str(path),str(invalid)],check=True)
        if name == 'Mifare Ultralight AES':
            signature = 'AES signature: ' + 'BC '*47 + 'BC\n'
            for index, extra in enumerate([
                'Highboy AES signature state: FF 1F 01\n' + signature,
                'Highboy AES signature state: FF 0F 04\n' + signature,
                'Highboy AES signature state: 01 00 01\n',
                'Highboy AES signature state: 00 00 01\n' * 2,
                'Highboy AES signature state: 00 01\n',
            ]):
                invalid=p/f'invalid-signature-{index}.nfc';invalid.write_text(contents+extra)
                subprocess.run([str(p/'test'),str(path),str(invalid)],check=True)
        if name == 'Mifare Ultralight C':
            unknown=p/'unknown-key.nfc'
            unknown.write_text(contents.replace('Pages read: 48','Pages read: 44'))
            subprocess.run([str(p/'test'),str(unknown)],check=True)
            invalid=p/'partial-ulc.nfc'
            invalid.write_text(contents.replace('Pages read: 48','Pages read: 43'))
            subprocess.run([str(p/'test'),paths[0],str(invalid)],check=True)
        if 'Ultralight 1' in name or 'Ultralight 2' in name:
            missing='\n'.join(line for line in contents.splitlines() if not line.startswith('Mifare version:'))+'\n'
            invalid=p/'missing-version.nfc';invalid.write_text(missing)
            subprocess.run([str(p/'test'),paths[0],str(invalid)],check=True)
    print('Type 2 file: fourteen profiles and metadata bounds passed')
    print('NTAG file: valid image and 16 malformed/incomplete profiles passed')
