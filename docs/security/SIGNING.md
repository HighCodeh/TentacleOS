<!--
SPDX-License-Identifier: GPL-3.0-or-later

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# External factory-image signatures

`security_signatures.py` consumes externally signed bootloader/application images
and an RSA-3072 public PEM (exponent 65537). It never accepts a private key or signs
an image. Keep production signing/HSM operations outside this repository.

For each target, sign the exact padded `app.bin` and `bootloader.bin` from its
verified factory review with that target's pinned SDK `espsecure` tool. Use
Secure Boot signature version 2. The private-key operation is deliberately not
part of this workflow. Return the two signed images and the public PEM, then run
with the same target's IDF Python:

```sh
python tools/highboy/security_signatures.py prepare \
  --review /tmp/p4-factory-review --app /path/to/signed-p4-app.bin \
  --bootloader /path/to/signed-p4-bootloader.bin --public-key /path/to/public.pem \
  --out /tmp/p4-signed-review
python tools/highboy/security_signatures.py verify /tmp/p4-signed-review
```

Repeat for C5 with its own reviewed inputs and SDK Python. Output must be new
and outside the checkout. Each signed image must preserve the exact reviewed
payload and append exactly one 4 KiB signature sector, within its checked region.
The official `espsecure` implementation verifies both signatures against the
provided public key. The report records the public DER fingerprint and actual
Secure Boot key digest, not a fabricated eFuse value. Independent checks reject
wrong keys, private keys, altered payload/signatures, truncation and later file
changes. Complete reviews are published atomically; an existing output is never
overwritten. Only bounded, regular input files are accepted.

A cryptographically valid signature does not identify an approved production
owner. `production_identity_verified`, `encryption_key_custody_verified`,
`deployment_approved`, `efuses_written` and `hardware_validated` remain false.
The review includes no per-device flash-encryption key or serial-write action.
An ordinary app-only updater/recovery candidate cannot deploy this factory map.

## Real-image regression test

```sh
python tools/highboy/test_security_images.py /tmp/p4-factory-review
python tools/highboy/test_security_images.py /tmp/c5-factory-review
```

Run each command using its matching IDF Python. Tests generate RSA and XTS keys
only in temporary directories, sign the actual built images, verify public-key
digests, reject malformed/foreign inputs, and round-trip both actual signed images through XTS
with wrong-key/address rejection. All fixture keys and signed fixture images are
removed on exit. They never become firmware trust identities or production
provisioning material. These tests exercise real host crypto, not chip eFuses,
ROM verification, physical flash encryption or device recovery.
