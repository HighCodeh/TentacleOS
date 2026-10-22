# Paired production security preparation

Issue #152 requires Secure Boot v2 and Flash Encryption on both chips. The
normal integrated build remains a development, app-only build. The profiles in
`integration/security/{p4,c5}.defaults` prepare the software configuration without
selecting or generating any private key and without accessing a device.

## Resolve against the actual SDK

First build/configure the ordinary pair with the pinned P4 v5.5.3 and C5 v6.0.1
lanes. Run once per target with that lane's IDF Python (which supplies kconfgen):

```sh
python3 tools/highboy/security.py --target p4 \
  --idf "$HIGHBOY_P4_IDF_PATH" --python "$HIGHBOY_P4_PYTHON_ENV/bin/python" \
  --build firmware_p4/build_highboy --out /private/tmp/highboy-security-p4
python3 tools/highboy/security.py --target c5 \
  --idf "$HIGHBOY_C5_IDF_PATH" --python "$HIGHBOY_C5_PYTHON_ENV/bin/python" \
  --build firmware_c5/build_highboy --out /private/tmp/highboy-security-c5
```

The output directory must be new. The tool reads the existing config/environment,
checks the exact SDK tag and target, resolves Kconfig into a private staging
directory and audits the result before publishing it. It does not change the
development sdkconfig, default profiles, build wrapper, flash updater or eFuses.
The board overlay, C5 memory settings, partitions, flash size and minimum revision
must remain intact. Resolution/auditing was exercised against both pinned SDKs;
unit tests reject missing requirements, unsafe flags, private-key selection,
mixed targets and altered C5 memory policy.

## Selected configuration

Both profiles select Secure Boot v2 RSA-3072, signed-image checks at boot/update,
XTS-AES-128 flash encryption and release mode. Signing during build and automatic
bootloader flashing are disabled: unsigned secure-profile artifacts would still
need external signing. Insecure boot, JTAG exceptions, short app partitions,
aggressive key revocation and virtual eFuses are rejected. These choices are a
reviewable configuration, not evidence of a provisioned device.

`review.json` records hashes and the unresolved signing/provisioning boundary.
When a baseline app exists, it also records a size projection for that existing
unsigned binary with default 64 KiB secure padding and a 4 KiB signature sector.
This is not a secure build: dependencies/code size may change when security is
enabled. A real signed bootloader/application build and slot check are required;
the current unsigned pair's projection exceeds the original slots. Do not enlarge
partitions, enable short-partition exceptions or bypass the check to conceal it.

## Remaining production handoff

The actual signing identities, independent per-device encryption-key custody,
chip revision/security-state inventory and reviewed recovery route remain to be
provided. These are separate from the Ed25519 `.hb` app-signing identity. No real
production identity was supplied; none is invented or substituted by these tools.
A secure-profile build must be signed externally, verified with its public key,
checked against both original slots and packaged with matching boot/assets before
any first boot. The normal updater deliberately refuses secured devices.

Do not flash a security-enabled bootloader as a configuration test: its first
boot can provision irreversible eFuses. This preparation tool contains no flash,
serial, key-generation or fuse-writing action. Hardware provisioning and recovery
approval remain a separate deployment step under the integrated branch contract.
