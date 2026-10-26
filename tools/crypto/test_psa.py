#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
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

"""Test production crypto against the installed SDK's real software PSA backend."""
import argparse
import hashlib
import hmac
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESCCM, AESGCM
from cryptography.hazmat.primitives import cmac
from cryptography.hazmat.primitives.kdf.hkdf import HKDF, HKDFExpand
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[2]


def run(command):
    subprocess.run(command, check=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf", type=Path, default=os.environ.get("IDF_PATH"))
    parser.add_argument("--build", type=Path)
    args = parser.parse_args()
    if args.idf is None:
        parser.error("--idf or IDF_PATH is required; this test needs the real SDK")
    source = args.idf / "components/mbedtls/mbedtls/tf-psa-crypto"
    notice = (ROOT / "common/crypto/firmware_crypto.h").read_text().split("#ifndef", 1)[0]
    with tempfile.TemporaryDirectory(prefix="tos-psa-") as directory:
        work = Path(directory)
        headers = work / "headers"
        (headers / "mbedtls").mkdir(parents=True)
        # ESP's builtin implementation retains two port-header includes. The host
        # software backend needs only their upstream declarations, no HAL emulation.
        for name in ("bignum", "ecp"):
            (headers / "mbedtls" / (name + ".h")).write_text(
                notice + '#include "mbedtls/private/' + name + '.h"\n')
        (headers / "esp_err.h").write_text(notice + "#pragma once\ntypedef int esp_err_t;\n"
            "#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n"
            "#define ESP_ERR_INVALID_ARG 0x102\n")
        (headers / "esp_log.h").write_text(notice + "#pragma once\n"
            "#define ESP_LOGW(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n")
        build = args.build or work / "build"
        cmake = os.environ.get("CMAKE", "cmake")
        run([cmake, "-S", str(source), "-B", str(build), "-DENABLE_TESTING=OFF",
             "-DENABLE_PROGRAMS=OFF", "-DGEN_FILES=OFF", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
             "-DCMAKE_C_FLAGS=-I" + str(headers), "-DPython3_EXECUTABLE=" + sys.executable])
        run([cmake, "--build", str(build), "--parallel", "4"])
        declarations = []
        cases = []

        def data(name, value):
            declarations.append("static const uint8_t " + name + "[] = {" +
                                ",".join(str(byte) for byte in value) + "};")
            return name

        key = bytes(range(16))
        key256 = bytes(range(32))
        iv = bytes(range(16, 32))
        plain = bytes(range(64))
        data("key", key)
        data("key256", key256)
        data("iv", iv)
        data("plain", plain)
        data("digest", hashlib.sha256(plain).digest())
        data("mac", hmac.digest(key, plain, "sha256"))
        cases += ["assert(firmware_crypto_sha256((firmware_crypto_span_t){plain,sizeof(plain)},out)==ESP_OK);",
                  "assert(memcmp(out,digest,sizeof(digest))==0);",
                  "assert(firmware_crypto_mac((firmware_crypto_span_t){key,sizeof(key)},(firmware_crypto_span_t){plain,sizeof(plain)},PSA_ALG_HMAC(PSA_ALG_SHA_256),out)==ESP_OK);",
                  "assert(memcmp(out,mac,sizeof(mac))==0);"]
        for mode, algorithm in ((modes.ECB(), "PSA_ALG_ECB_NO_PADDING"),
                                (modes.CBC(iv), "PSA_ALG_CBC_NO_PADDING"),
                                (modes.CTR(iv), "PSA_ALG_CTR")):
            expected = Cipher(algorithms.AES(key), mode).encryptor().update(plain)
            name = data("expected_" + algorithm, expected)
            vector = "{NULL,0}" if isinstance(mode, modes.ECB) else "{iv,sizeof(iv)}"
            cases.append("{ firmware_crypto_cipher_t p={.key={key,sizeof(key)},.iv=" + vector +
                         ",.algorithm=" + algorithm + ",.is_encrypt=true};" +
                         "assert(firmware_crypto_cipher(&p,(firmware_crypto_span_t){plain,sizeof(plain)},out)==ESP_OK);" +
                         "assert(memcmp(out," + name + ",sizeof(plain))==0); p.is_encrypt=false;" +
                         "assert(firmware_crypto_cipher(&p,(firmware_crypto_span_t){out,sizeof(plain)},out)==ESP_OK);" +
                         "assert(memcmp(out,plain,sizeof(plain))==0); }")
        for length in (0, 1, 17, 31):
            expected = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor().update(plain[:length])
            name = data("ctr_partial_" + str(length), expected or b"\x00")
            cases.append("{ firmware_crypto_cipher_t p={.key={key,sizeof(key)},.iv={iv,sizeof(iv)},"
                         ".algorithm=PSA_ALG_CTR,.is_encrypt=true};"
                         "assert(firmware_crypto_cipher(&p,(firmware_crypto_span_t){plain," + str(length) + "},out)==ESP_OK);"
                         "assert(memcmp(out," + name + "," + str(length) + ")==0); }")
        for length in (0, 1, 16, 31, 64):
            context = cmac.CMAC(algorithms.AES(key))
            context.update(plain[:length])
            name = data("cmac_" + str(length), context.finalize())
            cases.append("assert(nfc_aes_cmac(key,sizeof(key),plain," + str(length) + ",out)==0);" +
                         "assert(memcmp(out," + name + ",16)==0);")
        cases.append("{ uint8_t next[16]; nfc_cbc_params_t p={.key=key,.iv_in=iv,.iv_out=next,.encrypt=true};"
                     "assert(nfc_aes_cbc_crypt(&p,plain,sizeof(plain),out));"
                     "assert(memcmp(next,out+sizeof(plain)-16,16)==0);"
                     "memcpy(next,out+sizeof(plain)-16,16);uint8_t expected_iv[16];memcpy(expected_iv,next,16);"
                     "p.encrypt=false;assert(nfc_aes_cbc_crypt(&p,out,sizeof(plain),out));"
                     "assert(memcmp(out,plain,sizeof(plain))==0 && memcmp(next,expected_iv,16)==0);"
                     "assert(!nfc_aes_cbc_crypt(&p,plain,1,out)); }")
        for algorithm, nonce, tag_size in (("PSA_ALG_GCM", iv[:12], 16), ("PSA_ALG_CCM", iv[:13], 8)):
            for length in (0, 1, 31, 64):
                suffix = algorithm + str(length)
                name = data("nonce_" + suffix, nonce)
                cipher = AESGCM(key256) if tag_size == 16 else AESCCM(key256, tag_length=tag_size)
                expected = data("aead_" + suffix, cipher.encrypt(nonce, plain[:length], b"aad"))
                cases.append("{ uint8_t tag[16]; firmware_crypto_aead_t p={.cipher={.key={key256,sizeof(key256)},"
                    ".iv={" + name + ",sizeof(" + name + ")},.algorithm=PSA_ALG_AEAD_WITH_SHORTENED_TAG(" +
                    algorithm + "," + str(tag_size) + "),.is_encrypt=true},.aad={(const uint8_t *)\"aad\",3},"
                    ".tag=tag,.tag_size=" + str(tag_size) + "};"
                    "assert(firmware_crypto_aead(&p,(firmware_crypto_span_t){plain," + str(length) + "},out)==ESP_OK);"
                    "assert(memcmp(out," + expected + "," + str(length) + ")==0);"
                    "assert(memcmp(tag," + expected + "+" + str(length) + "," + str(tag_size) + ")==0);"
                    "p.cipher.is_encrypt=false;"
                    "assert(firmware_crypto_aead(&p,(firmware_crypto_span_t){out," + str(length) + "},out)==ESP_OK);"
                    "assert(memcmp(out,plain," + str(length) + ")==0);tag[0]^=1;"
                    "memset(out,0xa5,sizeof(out));"
                    "assert(firmware_crypto_aead(&p,(firmware_crypto_span_t){" + expected + "," + str(length) + "},out)!=ESP_OK);"
                    "for(size_t i=0;i<" + str(length) + ";++i)assert(out[i]==0); }")
        for expand in (False, True):
            algorithm = HKDFExpand if expand else HKDF
            options = {} if expand else {"salt": iv}
            expected = algorithm(algorithm=hashes.SHA256(), length=64, info=b"info", **options).derive(key256)
            name = data("hkdf_" + str(int(expand)), expected)
            cases.append("{ firmware_crypto_hkdf_t p={.secret={key256,sizeof(key256)},.salt={iv,sizeof(iv)},"
                         ".info={(const uint8_t *)\"info\",4},.is_expand_only=" + str(expand).lower() + "};"
                         "assert(firmware_crypto_hkdf(&p,out,64)==ESP_OK);assert(memcmp(out," + name + ",64)==0); }")
        data("alice", bytes.fromhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"))
        data("bob", bytes.fromhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"))
        data("shared", bytes.fromhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"))
        cases.append("assert(firmware_crypto_x25519(alice,bob,out)==ESP_OK);assert(memcmp(out,shared,32)==0);"
                     "uint8_t zero[32]={0};assert(firmware_crypto_x25519(alice,zero,out)!=ESP_OK);"
                     "assert(memcmp(out,zero,32)==0);")
        cases.append("for(unsigned i=0;i<256;++i){ uint8_t a[32],ap[32],b[32],bp[32],x[32],y[32];"
                     "assert(firmware_crypto_x25519_generate(a,ap)==ESP_OK);"
                     "assert(firmware_crypto_x25519_generate(b,bp)==ESP_OK);"
                     "assert(firmware_crypto_x25519(a,bp,x)==ESP_OK && firmware_crypto_x25519(b,ap,y)==ESP_OK);"
                     "assert(memcmp(x,y,32)==0 && memcmp(x,zero,32)!=0); }")
        first = ec.derive_private_key(1, ec.SECP256R1())
        second = ec.derive_private_key(2, ec.SECP256R1())
        data("p256_private", (1).to_bytes(32, "big"))
        data("p256_peer", second.public_key().public_bytes(serialization.Encoding.X962,
                                                          serialization.PublicFormat.UncompressedPoint))
        data("p256_shared", first.exchange(ec.ECDH(), second.public_key()))
        cases.append("{ psa_key_attributes_t attr=PSA_KEY_ATTRIBUTES_INIT;psa_key_id_t id=0,peer_id=0;size_t written=0;"
                     "psa_set_key_type(&attr,PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));"
                     "psa_set_key_bits(&attr,256);"
                     "assert(psa_import_key(&attr,p256_peer,sizeof(p256_peer),&peer_id)==PSA_SUCCESS);"
                     "assert(psa_destroy_key(peer_id)==PSA_SUCCESS);"
                     "psa_set_key_type(&attr,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));"
                     "psa_set_key_algorithm(&attr,PSA_ALG_ECDH);psa_set_key_usage_flags(&attr,PSA_KEY_USAGE_DERIVE);"
                     "assert(psa_import_key(&attr,p256_private,sizeof(p256_private),&id)==PSA_SUCCESS);"
                     "assert(psa_raw_key_agreement(PSA_ALG_ECDH,id,p256_peer,sizeof(p256_peer),out,sizeof(out),&written)==PSA_SUCCESS);"
                     "assert(written==32 && memcmp(out,p256_shared,32)==0);"
                     "assert(psa_destroy_key(id)==PSA_SUCCESS);"
                     "assert(psa_generate_key(&attr,&id)==PSA_SUCCESS);"
                     "assert(psa_export_public_key(id,out,sizeof(out),&written)==PSA_SUCCESS);"
                     "assert(written==65 && out[0]==4);assert(psa_destroy_key(id)==PSA_SUCCESS);psa_reset_key_attributes(&attr); }")
        nfc = ROOT / "firmware_p4/components/Applications/nfc/protocols/common"
        test = work / "test.c"
        test.write_text(notice + '#include <assert.h>\n#include <string.h>\n#include <stdio.h>\n'
                        '#include "firmware_crypto.h"\n#include "nfc_crypto.h"\n' + "\n".join(declarations) +
                        "\nint main(void){assert(psa_crypto_init()==PSA_SUCCESS);uint8_t out[128];\n" +
                        "\n".join(cases) + '\nputs("PASS: real SDK PSA SHA/HMAC/HKDF, AES ECB/CBC/CTR/CMAC/GCM/CCM, NFC IV, tamper rejection and RFC7748/P-256/key lifecycle");return 0;}\n')
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                   "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        for include in (headers, ROOT / "common/crypto", source / "include", source / "drivers/builtin/include", nfc, nfc / "include"):
            command += ["-I", str(include)]
        command += [str(test), str(ROOT / "common/crypto/firmware_crypto.c"),
                    str(nfc / "nfc_crypto.c"), str(nfc / "legacy_des/des.c"),
                    str(build / "core/libtfpsacrypto.a"), "-pthread", "-o", str(work / "test")]
        run(command)
        run([str(work / "test")])


if __name__ == "__main__":
    main()
