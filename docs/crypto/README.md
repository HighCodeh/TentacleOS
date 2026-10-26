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

# Public PSA cryptography

Both targets register `Service/firmware_crypto`, using the shared implementation
in `common/crypto`. It depends on the ESP-IDF `mbedtls` component and includes
only the public PSA interface. ESP-IDF initializes PSA at normal startup; callers
must run after startup. The host test initializes PSA explicitly.

The adapter provides SHA-256, HMAC-SHA256, AES-CMAC, HKDF-SHA256 (full or expand
only), AES ECB/CBC without padding, CTR, GCM/CCM and raw X25519. Callers select
protocol algorithms and supply their existing nonce/IV, AAD and tag lengths.
It neither changes wire formats nor persists keys. DES/3DES is an independent
[NFC compatibility exception](../nfc/README.md#idf-v61-cryptography).

Inputs are borrowed only for the call. Operations and volatile keys are local;
cleanup aborts operations and destroys keys on both success and failure. There
is no shared mutable adapter state. Cipher/AEAD calls allocate temporary output
so input/output may overlap and unauthenticated plaintext is never published.
They wipe temporary buffers; failed operations clear the caller's output after
argument validation. Callers must check the returned `esp_err_t` before using
output. CBC callers retain responsibility for protocol chaining IVs.

P4 selects software AES because the pinned v6.1 PSA hardware configuration
advertises CCM while the AEAD driver accepts only GCM. C5 retains hardware AES.
See [SDK compatibility](../highboy/README.md#esp-idf-v61-compatibility). This choice
preserves functionality; runtime performance has not been measured on hardware.

Run `python3 tools/crypto/test_psa.py --idf "$IDF_PATH"` with a native C compiler,
CMake and Python `cryptography` installed. It builds the SDK's actual TF-PSA-Crypto
software backend in temporary storage without changing the SDK. Two generated
header forwarders satisfy ESP port includes inside the library; application code
does not access private crypto APIs. Independent OpenSSL-backed Python vectors
and RFC 7748 cover the production adapter and NFC CBC/CMAC, tamper rejection,
in-place use, partial blocks and repeated volatile-key cleanup. The executable
uses ASan/UBSan; this is a host software check, not hardware-driver acceptance.
