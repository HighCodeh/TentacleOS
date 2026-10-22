#!/usr/bin/env python3
"""Real RSA/espsecure/XTS tests against a built factory review; fixture keys never leave temp storage."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
import esptool
import recovery
import secure_build
import security_signatures


def run(*arguments):
    name = arguments[0].replace('_', '-') if int(esptool.__version__.split('.')[0]) >= 5 else arguments[0]
    return subprocess.run([sys.executable, '-m', 'espsecure', name, *arguments[1:]],
                          check=True, timeout=60, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def rejected(operation, exceptions=(ValueError, subprocess.CalledProcessError)):
    try:
        operation()
    except exceptions:
        return
    raise AssertionError('Invalid security input was accepted')


def main():
    review = Path(sys.argv[1]).resolve()
    report = secure_build.verify(review)
    with tempfile.TemporaryDirectory(prefix='highboy-security-fixtures-') as temporary:
        directory = Path(temporary)
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        private = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                    serialization.NoEncryption())
        public = key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
        recovery.private_file(directory/'fixture-private.pem', private)
        (directory/'public.pem').write_bytes(public)
        rejected(lambda: security_signatures.public_key(private))
        small = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        rejected(lambda: security_signatures.public_key(small.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)))
        signed = {}
        for kind in ('app', 'bootloader'):
            run('sign_data', '--version', '2', '--keyfile', str(directory/'fixture-private.pem'),
                '--output', str(directory/(kind+'.bin')), str(review/secure_build.FILES[kind]))
            signed[kind] = (directory/(kind+'.bin')).read_bytes()
            assert len(signed[kind]) == report[kind]['signed_bytes']
        first = security_signatures.validate_signed(review, signed, public)
        assert len(first['secure_boot_key_digest']) == 64
        wrong = rsa.generate_private_key(public_exponent=65537, key_size=3072).public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
        rejected(lambda: security_signatures.validate_signed(review, signed, wrong))
        for kind in ('app', 'bootloader'):
            for offset in (0, len(signed[kind])-secure_build.SECTOR+50):
                bad = dict(signed);data=bytearray(bad[kind]);data[offset] ^= 1;bad[kind]=bytes(data)
                rejected(lambda: security_signatures.validate_signed(review, bad, public))
            bad=dict(signed);bad[kind]=bad[kind][:-1]
            rejected(lambda: security_signatures.validate_signed(review, bad, public))
        output = directory/'signed-review'
        security_signatures.prepare(review, directory/'app.bin', directory/'bootloader.bin', directory/'public.pem', output)
        result = security_signatures.verify(output)
        assert not result['deployment_approved'] and not result['production_identity_verified']
        rejected(lambda: security_signatures.prepare(review, directory/'app.bin', directory/'bootloader.bin', directory/'public.pem', output))
        path=output/'app.bin';data=bytearray(path.read_bytes());data[-100]^=1;path.write_bytes(data)
        rejected(lambda: security_signatures.verify(output))
        wrong_xts_key=directory/'wrong-xts.key';recovery.private_file(wrong_xts_key, os.urandom(32))
        xts = '--aes-xts' if int(esptool.__version__.split('.')[0]) >= 5 else '--aes_xts'
        for index, (kind, address) in enumerate((('bootloader', 0x2000), ('app', 0x20000))):
            plaintext = directory/(kind+'.bin')
            key_path=directory/f'xts-{index}.key';recovery.private_file(key_path, os.urandom(32))
            cipher=directory/f'cipher-{index}';decoded=directory/f'decoded-{index}'
            run('encrypt_flash_data', xts, '--keyfile', str(key_path), '--address', hex(address), '--output', str(cipher), str(plaintext))
            run('decrypt_flash_data', xts, '--keyfile', str(key_path), '--address', hex(address), '--output', str(decoded), str(cipher))
            assert decoded.read_bytes()==signed[kind] and cipher.read_bytes()!=signed[kind]
            run('decrypt_flash_data', xts, '--keyfile', str(key_path), '--address', hex(address+16), '--output', str(decoded), str(cipher))
            assert decoded.read_bytes()!=signed[kind]
            run('decrypt_flash_data', xts, '--keyfile', str(wrong_xts_key), '--address', hex(address), '--output', str(decoded), str(cipher))
            assert decoded.read_bytes()!=signed[kind]
    print('PASS:', report['target'], 'real built images, RSA-3072 signatures/public digests, foreign/private keys, tampering, truncation, immutable publication and XTS key/address round-trips; fixture identities only')


if __name__ == '__main__':
    main()
