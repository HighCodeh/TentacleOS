# TentacleOS app SDK (dev)

Tooling to build, sign, and package apps for the device. **Start with the guide:
[`docs/app_sdk/README.md`](../../docs/app_sdk/README.md)** — how to write, sign,
install, and run a functional app.

This directory is the working (dev-shaped) SDK; a clean self-contained package is
described in the canonical guide.

## Contents

- `build.sh` — compiles + signs every `<name>/app.c` into `<name>/app.hb`, without embedding apps into the firmware. Run with the IDF toolchain on
  PATH (`. $IDF_PATH/export.sh`).
- `hb_pack.py` — wraps a compiled ELF into a signed `.hb` (needs PyNaCl in the
  system python).
- `gen_key.py` — generates the Ed25519 signing keypair and regenerates the
  development trust-store header, overwriting both existing files. Do not run it
  for production; use the public-key build configuration in the guide.
- `keys/dev_ed25519.key` — the dev signing key (throwaway; production keys are
  managed separately and kept out of the repo).
- `hello/`, `blink/`, `counter/`, `alloc/`, `svc/`, `spin/`, `wifiscan/` — example
  apps, one `app.c` each (see the guide's example table). `wifiscan` shows the
  `api->wifi` subsystem.

Per-app `caps` file (hex, e.g. `0x80`) sets the capabilities the app requests;
default is `0x20` (ui).

`TOS_APP_SIGNING_KEY` selects an external private seed for `build.sh`. Firmware
trust is configured separately using `TOS_APP_PRODUCTION` and
`TOS_APP_TRUST_PUBLIC_KEY`; see [Keys / trust store](../../docs/app_sdk/README.md#keys--trust-store).
