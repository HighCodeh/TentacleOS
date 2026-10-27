#!/usr/bin/env python3
"""Real CMake-selected trust and production bundle verifier, using public test keys."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / 'firmware_p4/components/Service/app_runtime'
# RFC 8032 section 7.1 test vectors, never production identities.
TEST_SEEDS = (
    '9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60',
    '4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb',
)


def main():
    keys = [Ed25519PrivateKey.from_private_bytes(bytes.fromhex(seed)) for seed in TEST_SEEDS]
    # This seed is the repository's public, throwaway development fixture.
    keys.append(Ed25519PrivateKey.from_private_bytes(
        (ROOT / 'tools/app_sdk/keys/dev_ed25519.key').read_bytes()))
    public = keys[0].public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()
    cflags = shlex.split(os.environ.get('HOST_LINK_TEST_CFLAGS', ''))
    ldflags = shlex.split(os.environ.get('HOST_LINK_TEST_LDFLAGS', ''))
    if not cflags and not ldflags:
        cflags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'libsodium'], text=True))
        ldflags = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'libsodium'], text=True))
    with tempfile.TemporaryDirectory(prefix='hb-trust-') as temporary:
        work = Path(temporary)
        source = work / 'source'
        source.mkdir()
        build = work / 'build'
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(trust NONE)\n'
            'set(GENERATED_VERSION_DIR "${CMAKE_BINARY_DIR}/generated")\n'
            f'include("{RUNTIME}/trust_keys.cmake")\n')
        subprocess.run(['cmake', '-S', str(source), '-B', str(build),
                        '-DTOS_APP_PRODUCTION=ON', f'-DTOS_APP_TRUST_PUBLIC_KEY={public}'], check=True)
        (work / 'esp_err.h').write_text('''#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_INVALID_CRC 0x109
''')
        (work / 'esp_log.h').write_text('#define ESP_LOGE(tag, ...) ((void)(tag))\n')
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(work), '-I', str(build / 'generated'),
                   '-I', str(RUNTIME / 'include'), *cflags,
                   str(RUNTIME / 'tos_hb.c'), str(ROOT / 'tools/app_sdk/tests/test_signed_bundle.c')]
        custom, development = work / 'custom', work / 'development'
        subprocess.run([*command, '-DTOS_APP_HAS_CUSTOM_TRUST=1', '-o', str(custom), *ldflags], check=True)
        subprocess.run([*command, '-o', str(development), *ldflags], check=True)

        def signed(version, signer=0):
            elf = b'ELF\0test payload\0'
            meta = b'\x01\x05\x00radio\x02\x04\x00ICON' if version == 3 else b''
            header = struct.pack('<4sHHHHI16sII', b'HBAP', version, bool(meta), 1, 15, 8,
                                 b'radio-test', len(elf), len(meta))
            body = header + elf + meta
            return body + keys[signer].sign(body)

        cases = 0
        def check(binary, payload, expected):
            nonlocal cases
            path = work / 'fixture.hb'
            path.write_bytes(payload)
            subprocess.run([str(binary), str(path), str(expected)], check=True, timeout=30)
            cases += 1

        for version in (2, 3):
            trusted = signed(version)
            check(custom, trusted, 0)
            check(development, trusted, 0x109)
            check(custom, signed(version, signer=1), 0x109)
            check(development, signed(version, signer=2), 0)
            check(custom, signed(version, signer=2), 0x109)
            # Header capability/name, ELF, v3 metadata and every signature byte are signed.
            for offset in [12, 16, 40, *range(len(trusted) - 64, len(trusted))]:
                tampered = bytearray(trusted)
                tampered[offset] ^= 1
                check(custom, tampered, 0x109)
            if version == 3:
                tampered = bytearray(trusted)
                tampered[-65] ^= 1
                check(custom, tampered, 0x109)
            for end in (0, 39, 40, len(trusted) - 65, len(trusted) - 1):
                check(custom, trusted[:end], 0x104)
            for offset in (32, 36) if version == 3 else (32,):
                bad_length = bytearray(trusted)
                bad_length[offset:offset + 4] = b'\xff' * 4
                check(custom, bad_length, 0x104)
        print(f'PASS: {cases} real C/Python signed-bundle checks; CMake production trust replacement, '
              'v2/v3, metadata/capability/payload tampering, foreign keys and truncation')


if __name__ == '__main__':
    main()
