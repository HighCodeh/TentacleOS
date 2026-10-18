# Muse integration

The P4 owns the physical PTT UI and I2S recording in `Service/muse`.
`Service/highboy_muse` contains portable controllers, framing and adapters on
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
