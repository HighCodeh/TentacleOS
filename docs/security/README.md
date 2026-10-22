# Paired Secure Boot v2 and Flash Encryption

The P4 v5.5.3 and C5 v6.0.1 factory-security profiles build real bootloaders and
applications with Secure Boot v2 RSA-3072, signed-image checks at boot/update,
release-mode XTS-AES-128 and secure ROM download mode. Runtime flash-encryption
checks remain enabled. Insecure download/JTAG, validation skips, short app
partitions, virtual eFuses and automatic build signing are rejected.

These are **offline factory-installation review profiles**. `tools/build.sh`, the
normal partition CSVs and guarded app updater keep the existing development
profile. Neither security tool has a serial, flash, eFuse or key-generation action.
A successful build or signature check does not approve a device installation.

## Why a separate factory layout is required

The installed table is at `0x8000`, leaving 24 KiB after the bootloader offset
`0x2000`. Actual secure builds exceed that region; even compact trials that fit
an unsigned bootloader leave no room for its required 4 KiB signature sector.
Moving a check or weakening signature validation cannot make that installation
compatible. The new `integration/security/partitions-{p4,c5}.csv` files are selected
only through explicit factory profiles:

| Region | Development offset | Factory-review offset | Size |
| --- | --- | --- | --- |
| Partition table | `0x8000` | `0x10000` | 4 KiB sector |
| NVS | `0x9000` | `0x11000` | 24 KiB |
| OTA selection | `0xf000` | `0x17000` | 8 KiB |
| PHY data | `0x11000` | `0x19000` | 4 KiB |
| NVS keys | absent | `0x1a000` | 4 KiB, encrypted |

Both application slots, assets and coredump retain their original offsets and
sizes. P4 slots remain `0x2d0000`; C5 slots remain `0x200000`. Assets and coredump
are explicitly encrypted in the factory tables. NVS uses its own encryption
scheme, with keys in the flash-encrypted key partition; it must **not** have the
generic encrypted flag. Both lanes explicitly select the flash-encryption NVS
key provider, avoiding an implicit additional HMAC eFuse-key allocation on C5.

Existing plaintext NVS is not copied into encrypted NVS in place. Factory
installation requires an approved export/reseed or reset of those settings,
matching installed assets, OTA metadata and both signed images. The ordinary
updater and private development recovery tool deliberately reject this layout.
Production installation/recovery and device-data handling remain release work
in the [release checklist](../highboy/RELEASE.md#acceptance-outside-software-closure); no migration or first boot is performed by these tools.

## Resolve configuration only

The original `integration/security/{p4,c5}.defaults` still resolve against the
installed development layout for comparison. `security.py --factory-review`
additionally applies `{target}.factory.defaults`. SDK tags, the complete explicit
board overlay, C5 memory policy, flash size, minimum revision, bootloader offset
and MMU geometry are audited. The factory option permits only its specified
partition-table relocation and CSV selection; ordinary resolution preserves both.

```sh
"$HIGHBOY_P4_PYTHON_ENV/bin/python" tools/highboy/security.py --target p4 \
  --idf "$HIGHBOY_P4_IDF_PATH" --python "$HIGHBOY_P4_PYTHON_ENV/bin/python" \
  --build firmware_p4/build_highboy --out /tmp/p4-security-config --factory-review
```

Resolution is not compilation. The optional baseline size projection still
includes 64 KiB app padding plus a signature sector and explicitly records that
it is not a secure-build measurement.

## Build and audit actual images

Build the ordinary pair first, then run once per target with its IDF Python:

```sh
"$HIGHBOY_P4_PYTHON_ENV/bin/python" tools/highboy/secure_build.py build --target p4 \
  --idf "$HIGHBOY_P4_IDF_PATH" --python "$HIGHBOY_P4_PYTHON_ENV/bin/python" \
  --tools "$HIGHBOY_P4_TOOLS_PATH" --baseline firmware_p4/build_highboy \
  --out /tmp/p4-factory-review
"$HIGHBOY_C5_PYTHON_ENV/bin/python" tools/highboy/secure_build.py build --target c5 \
  --idf "$HIGHBOY_C5_IDF_PATH" --python "$HIGHBOY_C5_PYTHON_ENV/bin/python" \
  --tools "$HIGHBOY_C5_TOOLS_PATH" --baseline firmware_c5/build_highboy \
  --out /tmp/c5-factory-review
```

Output must be new and outside the checkout. The tool uses a fresh build directory,
verifies pinned SDK/submodule revisions, disables dependency downloads, removes
inherited compiler overrides and never reuses or modifies the ordinary build.
Failure leaves a `failed` report, which verification rejects.

Factory SDK code uses size optimization. P4 peripheral code retains `-O2`; C5
peripherals and Muse retain their original `-Og` policy. Existing per-file
policies and standalone emulator `-O2` settings remain. Assertions remain active
with silent messages; panic context/coredump still identifies their location.
The ordinary compiler and runtime configuration is unaffected. P4 accepts
`--app-production --app-public-key <64-public-hex-characters>` to select the
existing `.hb` production trust policy; a missing/development key is rejected
by its CMake validator. Default review builds retain development `.hb` trust
and record that mode, not production identity acceptance.

The final audit checks SDK-resolved security settings, image chip/checksum/SHA,
exact reviewed partition bytes, preserved app/data regions, encrypted data/key
flags, and actual padded sizes **plus the still-unappended 4 KiB signature sector**
for both app and bootloader. SDK unsigned size checks alone are insufficient.
`build.json` records hashes, capacities, source revision/dirty state and explicit
false deployment/provisioning/hardware gates. No signing key is selected.

```sh
"$HIGHBOY_P4_PYTHON_ENV/bin/python" tools/highboy/secure_build.py verify /tmp/p4-factory-review
```

## Signing and validation

See [external signature verification](SIGNING.md). CI builds both ordinary and
factory profiles in their pinned SDK lanes and tests the actual factory images
with temporary fixture identities. Artifacts are labeled factory-security review
evidence and contain no test private key, production identity or deployable
full-flash package. Host tests also cover config rejection, unchanged development
maps, table ordering/overlap and signature-space boundaries.

This completes the software integration requested by #152. Actual root-signing
identity, independent per-device flash-key custody, current chip/revision/eFuse
inventory, metadata migration/reset review, assets compatibility, paired first
boot and secure recovery remain deployment prerequisites. `.hb` Ed25519 app
trust is a separate identity, also required for a production release. No device
is claimed secured by these offline results; enabling these settings on first
boot can irreversibly change eFuses and debugging/recovery access.
