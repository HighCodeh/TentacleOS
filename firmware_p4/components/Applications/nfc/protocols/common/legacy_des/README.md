<!--
Copyright (c) 2025 HIGH CODE LLC

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

# NFC legacy DES/3DES

See the NFC subsystem documentation in `docs/nfc/README.md`.

These `des.c` and `des.h` sources originate from Mbed TLS v3.6.6, commit
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Their original dual Apache-2.0 /
GPL-2.0-or-later notices and upstream LICENSE are retained. Only the DES module
is included; this is not a second TLS implementation.

ESP-IDF v6.1 removed DES from TF-PSA-Crypto, while existing NFC card protocols
require DES/3DES. The local adaptation isolates configuration, byte-order helpers,
XOR and zeroization from the SDK; cipher tables and rounds remain upstream.
The public names are retained for the existing NFC callers. `des_support.h`
is the first-party portability layer. The upstream ECB/CBC self-tests are run
with ASan/UBSan by `tools/nfc/test_legacy_des.py`.

To update, compare the pinned upstream DES files, reapply the portability-only
changes, preserve notices, and rerun the vectors and NFC firmware build.
