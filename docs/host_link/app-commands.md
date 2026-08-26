# App manager commands (category `0x00` SYSTEM)

The companion app installs, lists, launches, stops, and removes on-device apps
(`.hb` bundles in `/sdcard/apps`). These ops are **P4-local** (handled by
`host_link_apps.c`, never relayed to the C5) and ride the normal host-link
`CMD`/`RESP` flow (see [`protocol.md`](./protocol.md)). All ops are in the SYSTEM
category. Multi-byte integers are little-endian.

Apps run only with capabilities the user/companion approved; the on-device
capability model (`tos_cap_t`) and signature/trust rules are the same ones the
loader enforces (see `APP_PLATFORM_PLAN.md`). Installing a bundle is a plain
`FILE_WRITE` (§10 of `protocol.md`) to `/sdcard/apps/<name>.hb`; the ops below
manage the lifecycle around it.

| op | id | request payload | response data |
|----|----|-----------------|---------------|
| `APP_LIST` | `0x54` | (none) | `[count u8]` then per app `[running u8][granted_caps u32][name_len u8][name]` |
| `APP_INSTALL` | `0x55` | `<name>` | (none) — status only |
| `APP_START` | `0x56` | `<name>` | (none) — status only |
| `APP_STOP` | `0x57` | `<name>` | (none) — status only |
| `APP_REMOVE` | `0x58` | `<name>` | (none) — status only |
| `APP_GRANT` | `0x59` | `<name>` | (none) — status only |

`<name>` is the app's base name (the `.hb` filename without extension), raw
bytes, no NUL terminator. The device rejects names containing `/`, `\`, or `..`.

## Ops

- **`APP_LIST`** — enumerates `/sdcard/apps/*.hb`. Each entry gives whether the
  app is currently running, the capabilities already granted to it
  (`tos_cap_t` bitmask), and its name. Requested capabilities are not returned
  here (they require opening each bundle); read the `.hb` header if you need them.

- **`APP_INSTALL`** — validates `/sdcard/apps/<name>.hb`: reads it and verifies
  the Ed25519 signature against the trust store. `OK` if the bundle is genuine,
  `ERROR` otherwise. Call this after `FILE_WRITE`-ing the bundle to confirm it
  installed cleanly.

- **`APP_GRANT`** — approves the app's requested capabilities (persisted in NVS,
  keyed by name). Reads the bundle to learn what it requests and grants exactly
  that set. Required before `APP_START` if the app requests any capability.

- **`APP_START`** — launches the app in its own supervised task. `INVALID_ARG`
  if the app requests capabilities that are not yet granted (call `APP_GRANT`
  first), `BUSY` if the app-slot limit is reached, `ERROR` on a bad/unreadable
  bundle, `OK` once started. The app then runs with `requested & granted` caps.

- **`APP_STOP`** — asks the running app named `<name>` to stop (cooperative,
  then force-kill after a timeout). `OK` if it was running, `ERROR` if not.

- **`APP_REMOVE`** — deletes `/sdcard/apps/<name>.hb` and clears its stored
  capability grant. Always `OK`.

## Status codes

Standard `spi_status_t` in the `RESP` (`protocol.md` §4): `0` OK, `1` BUSY (slots
full), `2` ERROR (bad/untrusted/unreadable bundle, or stop target not running),
`3` UNSUPPORTED, `4` INVALID_ARG (bad name, or start without the needed grant).

## Typical companion flow

```
FILE_WRITE  /sdcard/apps/blink.hb   (the signed bundle)
APP_INSTALL blink                   -> OK (signature verified)
APP_GRANT   blink                   -> OK (user approved its caps in the app UI)
APP_START   blink                   -> OK (running)
APP_LIST                            -> blink: running=1, granted=<ui>
APP_STOP    blink                   -> OK
APP_REMOVE  blink                   -> OK
```
