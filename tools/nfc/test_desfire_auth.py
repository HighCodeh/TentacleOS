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


"""Production DESFire/EV2 regression, NXP AN12343 vectors, OpenSSL host backend.
Does not qualify ESP PSA hardware, RF timing, DESFire emulation or complete SM.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NFC = ROOT / "firmware_p4/components/Applications/nfc"

def main():
    source = Path(os.environ.get("NFC_TEST_SOURCE_ROOT", str(ROOT))) / NFC.relative_to(ROOT)
    fixture = Path(os.environ.get("NFC_TEST_FIXTURE", str(Path(__file__).with_suffix(".c"))))
    prefix = os.environ.get("OPENSSL_PREFIX")
    if prefix is None and shutil.which("brew"):
        prefix = subprocess.check_output(["brew", "--prefix", "openssl@3"], text=True).strip()
    ssl_flags = ["-lcrypto"]
    if prefix:
        ssl = Path(prefix)
        ssl_flags = ["-I" + str(ssl / "include"), "-L" + str(ssl / "lib"), "-lcrypto"]
    with tempfile.TemporaryDirectory(prefix="hb-desfire-auth-") as tmp:
        out = Path(tmp)
        # Headers and implementation are compiled intact; only platform interfaces are mocked.
        stubs = {
            "esp_log.h": "#define ESP_LOGE(...) ((void)0)\n",
            "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
            "esp_random.h": "#include <stdint.h>\nuint32_t esp_random(void);\n",
            "highboy_nfc_error.h": "typedef enum {HB_NFC_OK=0, HB_NFC_ERR_PARAM=-1, HB_NFC_ERR_PROTOCOL=-2, HB_NFC_ERR_AUTH=-3, HB_NFC_ERR_INTERNAL=-4} hb_nfc_err_t;\n",
            "highboy_nfc_compat.h": "",
            "highboy_nfc_types.h": "typedef struct {int mock;} nfc_iso_dep_data_t; typedef struct {int mock;} nfc_iso14443a_data_t;\n",
            "iso_dep.h": "hb_nfc_err_t iso_dep_rats(int,int,nfc_iso_dep_data_t*); int iso_dep_apdu_transceive(nfc_iso_dep_data_t*,const uint8_t*,size_t,uint8_t*,size_t,int);\n",
            "firmware_crypto.h": """#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define ESP_OK 0
#define PSA_ALG_ECB_NO_PADDING 1
#define PSA_ALG_CBC_NO_PADDING 2
#define PSA_ALG_CMAC 3
typedef struct {const uint8_t *ptr; size_t len;} firmware_crypto_span_t;
typedef struct {firmware_crypto_span_t key,iv; int algorithm; bool is_encrypt;} firmware_crypto_cipher_t;
int firmware_crypto_cipher(const firmware_crypto_cipher_t*,firmware_crypto_span_t,uint8_t*);
int firmware_crypto_mac(firmware_crypto_span_t,firmware_crypto_span_t,int,uint8_t*);
""",
        }
        notice = "\n".join(Path(__file__).with_suffix(".c").read_text().splitlines()[:14]) + "\n"
        for name, data in stubs.items():
            (out/name).write_text(notice + data)
        mf = source / "protocols/mifare"
        common = source / "protocols/common"
        # Legacy DES is unchanged and outside this regression's positive qualification.
        includes = [out, mf/"include", common/"include", NFC/"protocols/common"]
        cmd = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-variable", "-Wno-deprecated-declarations", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all"]
        for include in includes:
            cmd += ["-I", str(include)]
        cmd += [str(fixture), str(mf/"mf_desfire.c"),
                str(common/"nfc_crypto.c"), str(NFC/"protocols/common/legacy_des/des.c"),
                *ssl_flags, "-o", str(out/"test")]
        subprocess.run(cmd, check=True, timeout=60)
        subprocess.run([str(out/"test")], check=True, timeout=30)

if __name__ == "__main__":
    main()
