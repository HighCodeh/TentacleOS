#!/usr/bin/env python3
"""Verify externally signed factory images against a reviewed unsigned build and public key."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import recovery
import secure_build

require = secure_build.require
BOUNDARY = {'schema_version': 1, 'status': 'signed_factory_security_review',
            'deployment_approved': False, 'factory_metadata_migration_required': True,
            'production_identity_verified': False, 'encryption_key_custody_verified': False,
            'efuses_written': False, 'hardware_validated': False,
            'normal_app_updater_supported': False}


def public_key(data):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    try:
        key = serialization.load_pem_public_key(data)
    except (ValueError, TypeError) as error:
        raise ValueError('An RSA public PEM is required; private keys are not accepted') from error
    require(isinstance(key, rsa.RSAPublicKey) and key.key_size == 3072 and
            key.public_numbers().e == 65537, 'Secure Boot v2 requires RSA-3072 with exponent 65537')
    return key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)


def validate_signed(review, signed, key_data):
    root_der = public_key(key_data)
    unsigned = secure_build.read_inputs(review)
    result = {}
    with tempfile.TemporaryDirectory(prefix='highboy-signature-') as temporary:
        directory = Path(temporary)
        key = directory/'public.pem';key.write_bytes(key_data)
        for kind in ('app', 'bootloader'):
            data = signed[kind]
            require(len(data) == len(unsigned[kind]) + secure_build.SECTOR and
                    data[:len(unsigned[kind])] == unsigned[kind],
                    'Signed '+kind+' differs from reviewed payload or signature-sector size')
            image = directory/(kind+'.bin');image.write_bytes(data)
            subprocess.run([sys.executable, '-m', 'espsecure', 'verify_signature', '--version', '2',
                            '--keyfile', str(key), str(image)], check=True, timeout=30,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            result[kind] = {'file': kind+'.bin', 'bytes': len(data), 'sha256': secure_build.digest(data)}
        digest_path = directory/'root-digest.bin'
        subprocess.run([sys.executable, '-m', 'espsecure', 'digest_sbv2_public_key',
                        '--keyfile', str(key), '--output', str(digest_path)], check=True, timeout=30,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        root_digest = digest_path.read_bytes()
        require(len(root_digest) == 32, 'Invalid Secure Boot public-key digest')
    return {'public_key_der_sha256': secure_build.digest(root_der),
            'secure_boot_key_digest': root_digest.hex(), 'images': result}


def verify(directory):
    directory = Path(directory)
    report = recovery.read_json(directory/'signatures.json')
    require(isinstance(report, dict), 'Invalid signature report')
    for name, expected in BOUNDARY.items():
        require(report.get(name) == expected and type(report.get(name)) is type(expected),
                'Invalid signature-review boundary: '+name)
    review = secure_build.verify(directory/'unsigned')
    key = recovery.read_regular(directory/'public.pem', 16*1024)
    signed = {kind: recovery.read_regular(directory/(kind+'.bin'),
              review[kind]['capacity_bytes']) for kind in ('app', 'bootloader')}
    actual = validate_signed(directory/'unsigned', signed, key)
    require(report.get('target') == review['target'], 'Signature target differs from build')
    require(report.get('unsigned_report_sha256') == secure_build.digest((directory/'unsigned/build.json').read_bytes()),
            'Unsigned review report changed')
    for name, value in actual.items():
        require(report.get(name) == value, 'Signed review content mismatch: '+name)
    return report


def prepare(review, app, bootloader, key, destination):
    report = secure_build.verify(review)
    destination = secure_build.outside_checkout(destination)
    require(not destination.exists(), 'Output already exists')
    signed = {kind: recovery.read_regular(path, report[kind]['capacity_bytes'])
              for kind, path in (('app', app), ('bootloader', bootloader))}
    key_data = recovery.read_regular(key, 16*1024)
    actual = validate_signed(review, signed, key_data)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.signed-review-', dir=destination.parent) as temporary:
        staging = Path(temporary)/'review';staging.mkdir(mode=0o700)
        (staging/'unsigned').mkdir(mode=0o700)
        for filename in (*secure_build.FILES.values(), 'build.json'):
            recovery.private_file(staging/'unsigned'/filename, recovery.read_regular(Path(review)/filename, 4*1024*1024))
        recovery.private_file(staging/'public.pem', key_data)
        for kind, data in signed.items():
            recovery.private_file(staging/(kind+'.bin'), data)
        result = dict(BOUNDARY, target=report['target'], **actual,
                      unsigned_report_sha256=secure_build.digest((staging/'unsigned/build.json').read_bytes()))
        recovery.private_file(staging/'signatures.json', (json.dumps(result, indent=2)+'\n').encode())
        verify(staging)
        staging.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('prepare')
    for name in ('review', 'app', 'bootloader', 'public-key', 'out'):
        create.add_argument('--'+name, type=Path, required=True)
    check = commands.add_parser('verify');check.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            output = prepare(args.review, args.app, args.bootloader, args.public_key, args.out)
            print('Signed factory-security review:', output)
        else:
            verify(args.directory)
            print('Signed factory-security review verified')
        print('Cryptographic verification only; production identity, device provisioning and deployment remain unapproved.')
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        raise SystemExit('Signature review failed: '+str(error)) from error


if __name__ == '__main__':
    main()
