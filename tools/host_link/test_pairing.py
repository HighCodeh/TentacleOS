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

"""Compile production security with real sodium/OpenSSL crypto and explicit OS/NVS fakes."""
import ctypes
import importlib.util
import struct
import types
from unittest.mock import patch
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
SERVICE=ROOT/'firmware_p4/components/Service'
sys.path.insert(0,str(ROOT/'tools/host_link'))
from hb_pairing import Handshake, mac16, verify_frame

class Pairing(ctypes.Structure):
    _fields_=[('token',ctypes.c_uint32),('code',ctypes.c_uint32),('is_approved',ctypes.c_bool),('can_pair',ctypes.c_bool)]

def fixtures(path):
    files={
      'esp_err.h': '#pragma once\n#include <stdint.h>\n#include <stddef.h>\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_INVALID_SIZE 0x104\n#define ESP_ERR_INVALID_CRC 0x109\nconst char *esp_err_to_name(esp_err_t);\n',
      'esp_log.h': '#pragma once\n#define ESP_LOGE(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n#define ESP_LOGI(tag,...) ((void)(tag))\n',
      'esp_timer.h': '#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
      'esp_mac.h': '#pragma once\n#include "esp_err.h"\n#define ESP_MAC_BASE 0\nesp_err_t esp_read_mac(uint8_t *,int);\n',
      'esp_random.h': '#pragma once\n#include <stddef.h>\nvoid esp_fill_random(void *,size_t);\n',
      'nvs.h': '#pragma once\n#include "esp_err.h"\ntypedef int nvs_handle_t;\n#define NVS_READWRITE 0\nesp_err_t nvs_open(const char *,int,nvs_handle_t *);\nesp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);\nesp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);\nesp_err_t nvs_commit(nvs_handle_t);\nvoid nvs_close(nvs_handle_t);\n',
      'freertos/FreeRTOS.h': '#pragma once\n#include <stdint.h>\n#define portMAX_DELAY UINT32_MAX\n',
      'freertos/semphr.h': '#pragma once\n#include <stdint.h>\ntypedef void *SemaphoreHandle_t;\nvoid *xSemaphoreCreateRecursiveMutex(void);\nint xSemaphoreTakeRecursive(void *,uint32_t);\nint xSemaphoreGiveRecursive(void *);\n',
      'mbedtls/md.h': '#pragma once\n#include <stdint.h>\n#include <stddef.h>\ntypedef struct {int unused;} mbedtls_md_info_t;\n#define MBEDTLS_MD_SHA256 1\nconst mbedtls_md_info_t *mbedtls_md_info_from_type(int);\nint mbedtls_md_hmac(const mbedtls_md_info_t *,const uint8_t *,size_t,const uint8_t *,size_t,uint8_t *);\n',
      'mbedtls/hkdf.h': '#pragma once\n#include "mbedtls/md.h"\nint mbedtls_hkdf(const mbedtls_md_info_t *,const uint8_t *,size_t,const uint8_t *,size_t,const uint8_t *,size_t,uint8_t *,size_t);\n',
      'mbedtls/platform_util.h': '#pragma once\n#include <stddef.h>\nvoid mbedtls_platform_zeroize(void *,size_t);\n'}
    files['firmware_crypto.h'] = (ROOT / 'tools/crypto/fixtures/firmware_crypto.h').read_text()
    for name,text in files.items():
        dest=path/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text((ROOT / "common/crypto/firmware_crypto.h").read_text().split("#ifndef", 1)[0] + text)

def interop(path):
    lib=ctypes.CDLL(str(path))
    lib.pairing_test_boot()
    lib.host_link_sec_pairing_window(True)
    lib.host_link_sec_pairing_get.argtypes=[ctypes.POINTER(Pairing)]
    lib.host_link_sec_pairing_get.restype=ctypes.c_bool
    lib.host_link_sec_is_authenticated.restype=ctypes.c_bool
    lib.host_link_sec_finish.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_uint32]
    lib.host_link_sec_finish.restype=ctypes.c_bool
    lib.host_link_sec_handle_hello.argtypes=[ctypes.c_void_p,ctypes.c_uint16,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
    client=Handshake(bytes(range(1,33)))
    ack=ctypes.create_string_buffer(71);length=ctypes.c_size_t()
    assert lib.host_link_sec_handle_hello(client.hello,len(client.hello),ack,71,ctypes.byref(length))==0
    for index in (0,1,17,49,55,70):
        altered=bytearray(ack.raw);altered[index]^=1
        try:client.accept(bytes(altered))
        except PermissionError:pass
        else:raise AssertionError('client accepted altered handshake transcript')
    assert client.accept(ack.raw)==b'ABCDEF'
    pairing=Pairing();assert lib.host_link_sec_pairing_get(ctypes.byref(pairing))
    assert pairing.code==client.code and not lib.host_link_sec_is_authenticated()
    span=bytes.fromhex('0101010000000300120000');mac=mac16(client.k_a2d,span)
    assert not lib.host_link_sec_finish(span,len(span),mac,1)
    assert lib.host_link_sec_pairing_confirm(pairing.token,True)==0
    assert lib.host_link_sec_finish(span,len(span),mac,1)
    out=ctypes.create_string_buffer(16)
    assert lib.host_link_sec_sign_outbound(span,len(span),out)==1
    assert out.raw==mac16(client.k_d2a,span)
    print('PASS: production C security interoperates with Python cryptography X25519 and matches confirmation code/directional keys')

def clients(lib):
    class Serial:
        def __init__(self):
            self.queue=bytearray();self.finishes=0
        def write(self,frame):
            body=frame[10:-16] if frame[3] else frame[10:]
            if body[0]==0x10:
                ack=ctypes.create_string_buffer(71);length=ctypes.c_size_t()
                assert lib.host_link_sec_handle_hello(body[3:],len(body)-3,ack,71,ctypes.byref(length))==0
                payload=bytes([0x11,0,0])+ack.raw
                self.queue+=b'HB'+bytes([1,0])+struct.pack('<IH',0,len(payload))+payload
            elif body[0]==0x12:
                self.finishes+=1
                if self.finishes==1:
                    assert not lib.host_link_sec_finish(frame[2:-16],len(frame)-18,frame[-16:],1)
                    pairing=Pairing();assert lib.host_link_sec_pairing_get(ctypes.byref(pairing))
                    assert lib.host_link_sec_pairing_confirm(pairing.token,True)==0
                assert lib.host_link_sec_finish(frame[2:-16],len(frame)-18,frame[-16:],1)
                ready=b'HB'+bytes([1,1])+struct.pack('<IH',1,3)+bytes([0x13,0,0])
                out=ctypes.create_string_buffer(16)
                assert lib.host_link_sec_sign_outbound(ready[2:],len(ready)-2,out)
                self.queue+=ready+out.raw
            else:
                raise AssertionError('unexpected client frame')
        def read(self,count):
            result=bytes(self.queue[:count]);del self.queue[:count];return result
    serial=types.ModuleType('serial');tools=types.ModuleType('serial.tools')
    tools.list_ports=types.SimpleNamespace(comports=lambda:[])
    for name in ('cli','hb_push'):
        lib.host_link_sec_reset();lib.host_link_sec_pairing_window(True)
        spec=importlib.util.spec_from_file_location('pairing_test_'+name,ROOT/'tools/host_link'/f'{name}.py')
        module=importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules,{'serial':serial,'serial.tools':tools}):spec.loader.exec_module(module)
        link=object.__new__(module.Link);link.ser=Serial();link.buf=bytearray();link.k_a2d=link.k_d2a=None;link.tx_counter=0
        assert link.handshake(bytes(range(1,33)))=='41:42:43:44:45:46'
        assert lib.host_link_sec_is_authenticated() and link.tx_counter==2 and link.rx_counter==1
        valid=b'HB'+bytes([1,1])+struct.pack('<IH',2,3)+bytes([2,0,0])
        valid+=mac16(link.k_d2a,valid[2:]);link.ser.queue+=valid
        assert link._read_frame(1e30)==valid
        link.ser.queue+=valid
        try:link._read_frame(1e30)
        except PermissionError:pass
        else:raise AssertionError('client accepted a replay')
        invalid=bytearray(valid);invalid[-1]^=1;link.ser.queue+=invalid
        try:link._read_frame(1e30)
        except PermissionError:pass
        else:raise AssertionError('client accepted a forged response')
    print('PASS: both actual USB clients finish the C handshake and reject replayed/forged responses; serial/UI approval are explicit fakes')


def ui_test(command,tmp):
    source=(ROOT/'firmware_p4/components/Applications/ui/screens/bluetooth/ble_companion_ui.c').read_text()
    start=source.index('static void companion_input(const input_event_t *ev, void *ctx) {')
    function=source[start:]
    prefix='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define INPUT_ACTION_PRESS 1
#define INPUT_BTN_OK 1
#define INPUT_BTN_BACK 2
#define SCREEN_BLE_MENU 3
#define ESP_OK 0
typedef struct {int action,button;} input_event_t;
static void *s_phase_timer,*s_footer;
static uint32_t s_pairing_token;
static bool s_is_linked;
static unsigned allows,rejects,stops,exits,footers,resets;
static int host_link_sec_pairing_confirm(uint32_t token,bool approved) {assert(token==17);if(approved)++allows;else++rejects;return 0;}
static void host_link_sec_pairing_window(bool open) {assert(!open);}
static void ui_chrome_footer_set_text(void *p,const char *text) {(void)p;assert(text);++footers;}
static void lv_timer_delete(void *p) {assert(p);}
static void host_link_reset_rx(void) {++resets;}
static void host_link_ble_stop(void) {++stops;}
static void ui_switch_screen(int id) {assert(id==3);++exits;}
'''
    suffix='''
int main(void) {
 input_event_t event={.action=INPUT_ACTION_PRESS,.button=INPUT_BTN_OK};
 companion_input(&event,NULL);assert(!allows);
 s_pairing_token=17;companion_input(&event,NULL);assert(allows==1 && footers==1);
 event.action=0;companion_input(&event,NULL);assert(allows==1);
 event.action=INPUT_ACTION_PRESS;event.button=INPUT_BTN_BACK;companion_input(&event,NULL);
 assert(rejects==1 && stops==1 && exits==1 && resets==1);
 s_is_linked=true;s_pairing_token=0;companion_input(&event,NULL);assert(stops==1 && exits==2 && resets==1);
 return 0;
}
'''
    (tmp/'ui.c').write_text(prefix+function+suffix)
    cc=[*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
    subprocess.run([*cc,str(tmp/'ui.c'),'-o',str(tmp/'ui')],check=True,timeout=60)
    subprocess.run([str(tmp/'ui')],check=True,timeout=30)
    print('PASS: production physical OK/BACK handler confirms only a displayed token and cancels unlinked pairing')

def main():
    cflags=shlex.split(os.environ.get('HOST_LINK_TEST_CFLAGS',''))
    ldflags=shlex.split(os.environ.get('HOST_LINK_TEST_LDFLAGS',''))
    if not ldflags:
        cflags+=shlex.split(subprocess.check_output(['pkg-config','--cflags','libsodium','openssl'],text=True))
        ldflags=shlex.split(subprocess.check_output(['pkg-config','--libs','libsodium','openssl'],text=True))
    with tempfile.TemporaryDirectory(prefix='tos-pairing-') as tmp:
        tmp=Path(tmp);fixtures(tmp)
        command=[*shlex.split(os.environ.get('CC','cc')),'-std=c11','-D_XOPEN_SOURCE=700','-Wall','-Wextra','-Werror','-pthread',
                 '-I',str(tmp),'-I',str(SERVICE/'host_link/include'),*cflags,
                 str(SERVICE/'host_link/host_link_sec.c'),str(ROOT/'tools/host_link/tests/pairing_crypto_shim.c')]
        subprocess.run([*command,'-fsanitize=address,undefined','-fno-sanitize-recover=all',
                       str(ROOT/'tools/host_link/tests/test_pairing.c'),*ldflags,'-o',str(tmp/'test')],check=True,timeout=60)
        subprocess.run([str(tmp/'test')],check=True,timeout=30)
        subprocess.run([*command,'-shared','-fPIC',*ldflags,'-o',str(tmp/'pairing.so')],check=True,timeout=60)
        interop(tmp/'pairing.so')
        clients(ctypes.CDLL(str(tmp/'pairing.so')))
        ui_test(command,tmp)

if __name__=='__main__':main()
