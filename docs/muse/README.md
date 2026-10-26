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

# Muse integration

The P4 owns the physical PTT UI and I2S recording in `Service/muse`.
`Service/muse_port` contains portable controllers, framing and adapters on
both targets. The C5 owns BLE pairing, account/token storage, SDK execution and
network connections. Credentials do not cross into the P4 UI.

On P4, the recorder runs at `SYS_PRIO_SERVICE_HI` on `SYS_CORE_UI`; the SPI/control
worker runs at `SYS_PRIO_SERVICE_LO` on `SYS_CORE_RADIO`. Recording buffers belong
to the P4 worker and are freed only after the recorder exits. Allocation failures
are logged without audio, token or account contents.

C5 setup, runtime and account tasks use `SYS_PRIO_SERVICE_LO`; token-UART work
uses `SYS_PRIO_BACKGROUND`. All are pinned to `SYS_CORE_MAIN`, retaining the
explicit internal-stack capability requirements. The scheduling header is a
private include of the standalone Muse component, avoiding a new component
requirement cycle.

Pairing requires the existing physical confirmation flow. Voice notes receive
text replies; spoken speaker replies are not implemented. USB voice remains
disabled. Offline Grok menu animation is a separate feature.

See [integration/build limits](../highboy/README.md) and
[canonical host tests](../highboy/TESTS.md) for the preserved protocol v8,
C5 memory overlay, exact dependency versions and device acceptance boundary.

The native P4 RPC helper groups payload data and length in a typed input record.
It enforces the existing payload bound before narrowing the size for SPI; the
wire format and command values are unchanged.

Both chips now build with ESP-IDF v6.1. The C5 SDK pairing adapter retains its
pinned private ECC/GCM primitive dependency; the Muse component is optimized
for size to preserve the existing OTA slot. SDK support components are also
optimized for size to retain the existing internal-RAM headroom requirement.

## Public PSA cryptography

The C5 pairing adapter uses volatile PSA P-256 keys, public ECDH, SHA-256,
HMAC/HKDF and AES-GCM APIs. Public-key encoding, transcript, record AAD, nonces
and tags are unchanged. Reset destroys the ephemeral key and clears session
secrets. Hardware-backed identity signing keeps its existing PSA policy and
performs no provisioning or eFuse writes. Noise continues to select its existing
`PsaCryptoBackend.cpp`; the upstream Mbed TLS backend is retained but not built.
