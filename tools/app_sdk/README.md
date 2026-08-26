# TentacleOS app SDK (dev)

Tooling to build, sign, and package apps for the device. **Start with the guide:
[`docs/app_sdk/README.md`](../../docs/app_sdk/README.md)** — how to write, sign,
install, and run a functional app.

This directory is the working (dev-shaped) SDK; a clean self-contained package is
Phase 8 of `APP_PLATFORM_PLAN.md`.

## Contents

- `build.sh` — compiles + signs every `<name>/app.c` into `<name>/app.hb`, and
  regenerates the embedded copy the firmware ships. Run with the IDF toolchain on
  PATH (`. $IDF_PATH/export.sh`).
- `hb_pack.py` — wraps a compiled ELF into a signed `.hb` (needs PyNaCl in the
  system python).
- `gen_key.py` — generates the Ed25519 signing keypair and regenerates the
  firmware trust-store header. Reflash after running it.
- `keys/dev_ed25519.key` — the dev signing key (throwaway; production keys are
  managed separately and kept out of the repo).
- `hello/`, `blink/`, `counter/`, `alloc/`, `svc/`, `spin/` — example apps, one
  `app.c` each (see the guide's example table).

Per-app `caps` file (hex, e.g. `0x80`) sets the capabilities the app requests;
default is `0x20` (ui).
