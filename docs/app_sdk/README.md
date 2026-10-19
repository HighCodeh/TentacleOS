# Writing an app for TentacleOS

This is the practical guide to building a **functional app** that runs on the
device without reflashing the firmware: write it on a PC, compile it, sign it,
drop it on the SD card, and launch it.

An app is a small native RISC-V program. It links against **nothing** in the
firmware: at entry it receives one pointer, `const tos_api_t *api`, and reaches
every device function through it. It runs from PSRAM in its own supervised task,
with only the capabilities the user approved.

Tooling lives in [`tools/app_sdk/`](../../tools/app_sdk). The published ABI is
`Service/app_runtime/include/tos_api.h`; the companion wire operations are in
[`host_link/app-commands.md`](../host_link/app-commands.md).

---

## 1. Prerequisites

- **The ESP-IDF RISC-V toolchain** on PATH (same one the firmware uses):
  `. $IDF_PATH/export.sh` puts `riscv32-esp-elf-gcc` on PATH.
- **PyNaCl** for signing (in your *system* python, not the IDF venv):
  `pip install pynacl`.
- **A signing key trusted by the device.** The firmware only runs apps signed by
  a key in its trust store (`firmware_p4/components/Service/app_runtime/include/tos_trust_keys.h`).
  The dev key is `tools/app_sdk/keys/dev_ed25519.key`; the matching public key is
  already embedded in development builds. To select your own public key, use
  the build configuration in §6. Changing the trusted identity requires a new
  firmware candidate and the reviewed deployment process.

---

## 2. Quick start

```sh
cd tools/app_sdk
. $IDF_PATH/export.sh          # toolchain on PATH
./build.sh                     # compiles + signs every app_sdk/<name>/app.c -> <name>/app.hb
```

Copy `tools/app_sdk/hello/app.hb` to `/sdcard/apps/hello.hb` (card reader, or the
companion `FILE_WRITE`). Then on the device console:

```
apprun hello grant             # first run: approve its capabilities, then run
```

Or, on the device UI: **menu -> DEV -> Apps**, pick the app, press OK, and approve
the capability prompt. Apps live only on the SD card (`/sdcard/apps/*.hb`); the
firmware ships no built-in apps.

---

## 3. Writing the app

A minimal app is one `app.c` with an `app_main`:

```c
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  api->log(TOS_LOG_INFO, "MYAPP", "hello from my app");
  api->led_set(0, 255, 0);     // needs the UI capability
  return 0;                      // fire-and-forget: runs once and exits
}
```

**Rules**
- Include only `tos_api.h` (from `Service/app_runtime/include`). Do **not** include
  firmware or IDF headers, and do not call libc (`printf`, `malloc`, …). Use the
  `api->` calls. The app compiles to a relocatable object with no external
  symbols; a stray libc/firmware call becomes an unresolved symbol the loader
  cannot satisfy at load time.
- `app_main` is the entry (resolved by symbol name). Its return value is logged.
- Reach everything through `api`. Capabilities you use must be granted (§4, §7).

### Long-running apps (daemons)

An app that keeps running must poll `api->should_stop()` and exit when asked, so
`appstop` can stop it cleanly:

```c
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc; (void)argv;
  int on = 0;
  while (!api->should_stop()) {
    on = !on;
    api->led_set(0, on ? 40 : 0, 0);
    api->delay_ms(300);          // yields and feeds the watchdog
  }
  api->led_set(0, 0, 0);
  return 0;
}
```

An app that loops **without** yielding (`delay_ms`/`yield`) or ignores
`should_stop` will be force-killed after a timeout — do not rely on that.

An app driving a radio should also poll `api->resource_lost()`: it goes nonzero
when the on-device UI or the companion preempts a radio the app held (the app's
C5 session is already stopped), so the app can stop or back off instead of
talking to a session that is gone:

```c
while (!api->should_stop() && !api->resource_lost()) {
  // ... use the session started earlier ...
  api->delay_ms(200);
}
```

---

## 4. The host API (`tos_api_t`)

The full contract is `Service/app_runtime/include/tos_api.h`. ABI version 1.10.

| Call | Capability | Notes |
|------|-----------|-------|
| `log(level, tag, fmt, …)` | — | `level` is `tos_log_level_t` |
| `mem_alloc(n)` / `mem_free(p)` | — | charged to the app's 1 MB arena; leaks reclaimed on exit |
| `delay_ms(ms)` | — | blocks, feeds the watchdog |
| `yield()` | — | cooperative yield (also feeds the watchdog) |
| `should_stop()` | — | nonzero once `appstop` was called |
| `resource_lost()` | — | nonzero once a radio the app held was preempted by the UI/companion |
| `host_link_register(h)` / `host_link_unregister(cat, op)` | `hostlink` | add a companion command; auto-removed on exit |
| `console_register(cmd, help, func)` / `console_unregister(cmd)` | `console` | add a shell command; auto-removed on exit |
| `led_set(r, g, b)` | `ui` | RGB status LED |

The surface grows additively (minor ABI bumps); an app built against 1.x keeps
running on any firmware with the same major and a >= minor.

### Subsystems (`api->wifi`, …)

Bigger areas are their own sub-struct reached by a pointer, so calls read as
`api->wifi->scan()`. WiFi came first (ABI 1.2) and BLE followed (ABI 1.3); the
same shape is how every future subsystem (ir, subghz, …) is added.

**`api->wifi`** — full reference in `tos_api.h` (`tos_wifi_api_t`):

| Call | Capability | Notes |
|------|-----------|-------|
| `scan()` | `radio-rx` | blocks until the scan finishes |
| `ap_count()` / `ap_get(i, &ap)` | `radio-rx` | read results after `scan()`; `ap` is `tos_wifi_ap_t` |
| `is_connected()` / `connected_ssid()` | — | station state |
| `connect(ssid, pass)` / `disconnect()` | `radio-tx` | join / leave an AP |
| `sniffer_start(type, ch, monitor, &sid)` | `radio-rx` | 802.11 sniffer; returns a session id |
| `deauth_detect(&sid)` / `probe_monitor(&sid)` / `signal_monitor(bssid, ch, &sid)` | `radio-rx` | monitors |
| `deauth(bssid, client, type, ch, &sid)` | `radio-tx` | targeted deauth session |
| `deauth_broadcast(bssid, type, ch)` | `radio-tx` | single-shot broadcast deauth |
| `flood(type, bssid, ch, &sid)` | `radio-tx` | auth/assoc/probe flood |
| `beacon_spam(ssid_list_path, &sid)` | `radio-tx` | `NULL` = random SSIDs |
| `evil_twin(ssid, &sid)` | `radio-tx` | fake AP + captive portal |
| `session_stop(sid)` | — | stop any monitor/attack session |
| `channel_hop(enable)` | `radio-tx` | radio channel hopping |

The app never sees the SPI ids or byte layouts underneath — just typed calls.
This is the reference for exposing firmware to apps (see "Extending the ABI").

**`api->ble`** — full reference in `tos_api.h` (`tos_ble_api_t`). The C5 serialises
the whole Bluetooth host, so BLE is **one-owner**: any call returns
`ESP_ERR_INVALID_STATE` (BUSY) when the companion BLE link or another app holds
the radio. That is by design — an app attack can never tear down the companion's
own BLE channel.

| Call | Capability | Notes |
|------|-----------|-------|
| `scan(duration_ms)` | `radio-rx` | blocks until the scan finishes (`0` = 5 s) |
| `device_count()` / `device_get(i, &d)` | `radio-rx` | read results after `scan()`; `d` is `tos_ble_device_t` |
| `connect(addr, addr_type)` / `disconnect()` | `radio-tx` | connect / drop |
| `set_random_mac()` | `radio-tx` | MAC spoof |
| `adv_start()` / `adv_stop()` | `radio-tx` | advertising |
| `sniffer_start(&sid)` | `radio-rx` | advertisement sniffer; returns a session id |
| `spam(attack_index, &sid)` | `radio-tx` | adv spam (0 Apple Juice, 1 Sour Apple, 2 Swift Pair, 3 Samsung, 4 Android, 5 Tutti Frutti) |
| `flood(addr, addr_type, &sid)` | `radio-tx` | connect flood |
| `skimmer_detect(&sid)` / `tracker_detect(&sid)` | `radio-rx` | detectors (tracker = AirTag etc.) |
| `session_stop(sid)` | — | stop any monitor/attack session |
| `hid_start()` / `hid_stop()` | `hid` | BadBLE HID keyboard: advertise a HID peripheral, held until stop |
| `hid_is_connected()` | `hid` | is a target connected? |
| `hid_send_key(modifier, keycode)` | `hid` | one USB-HID key (send, then a zero key to release) |

---

## 5. Capabilities

An app declares what it needs; the user approves it once (persisted); each `api`
call re-checks the grant. An ungranted call returns an error, it does not crash.

| Bit | Name | Grants |
|----:|------|--------|
| `0x01` | `fs-read` | read files |
| `0x02` | `fs-write` | write files |
| `0x04` | `radio-tx` | WiFi/BT/SubGHz/LoRa/IR transmit |
| `0x08` | `radio-rx` | radio receive |
| `0x10` | `hid` | BadUSB / HID injection |
| `0x20` | `ui` | screen / LED |
| `0x40` | `hostlink` | register companion commands |
| `0x80` | `console` | register console commands |

Pass the OR of the bits your app uses as `--caps` when packing (§6). Request the
minimum: the consent prompt shows exactly what you ask for.

---

## 6. Build, sign, and package

### The `.hb` bundle

```
[ 40-byte header ][ the ELF ][ 64-byte Ed25519 signature ]
  magic "HBAP", format ver, abi major/minor, caps, name, elf_len
  signature = Ed25519(header || elf)
```

### One command

`build.sh` compiles and signs every `app_sdk/<name>/app.c`. Put your app in its own
dir (`tools/app_sdk/myapp/app.c`), optionally a `caps` file (hex, default `0x20`),
then run `./build.sh`. The script supports BSD/macOS and GNU/Linux byte counts
without GNU `stat`; its regression test also covers paths with spaces and an
external signing-key path. Output is `<name>/app.hb` — copy it to `/sdcard/apps/`.

### By hand

```sh
# 1. compile as a relocatable object (ET_REL), no PIC, relaxation off
riscv32-esp-elf-gcc -march=rv32imafc_zicsr_zifencei -mabi=ilp32f \
  -Os -ffreestanding -fno-common -mno-relax -ffunction-sections -fdata-sections \
  -I firmware_p4/components/Service/app_runtime/include \
  -c -o myapp.elf myapp.c

# 2. wrap + sign (system python; --caps is the OR of §5 bits)
/usr/bin/python3 tools/app_sdk/hb_pack.py myapp.elf myapp.hb \
  --name myapp --caps 0x20 --sign tools/app_sdk/keys/dev_ed25519.key
```

`-c` (object, not a PIE), `-mno-relax`, and `-fdata-sections` matter: the loader is
a section-based ET_REL relocator, so it needs per-section, un-relaxed relocations.

### Keys / trust store

Development builds keep the existing throwaway public key by default. A custom
trust store is generated **only in the P4 build directory**; it replaces the dev
identity rather than trusting both. Source headers, C5 firmware, SPI protocol and
bundle format remain unchanged.

```sh
# PUBLIC key: exactly 64 hexadecimal characters (32 raw Ed25519 bytes).
TOS_APP_PRODUCTION=ON TOS_APP_TRUST_PUBLIC_KEY=<public-key-hex> bash tools/build.sh p4

# Signing is separate, on the signing machine. Never commit the private seed.
TOS_APP_SIGNING_KEY=/secure/path/app-signing.seed bash tools/app_sdk/build.sh
```

`TOS_APP_PRODUCTION=ON` refuses a missing key, malformed hex, an all-zero key or
the shipped development identity (including uppercase hex). This gate validates
configuration; it does not certify custody of a real production key. No real
production identity has been provisioned yet. `TOS_APP_TRUST_PUBLIC_KEY` may also
be set without production mode for testing a custom identity. The integrated
wrapper explicitly selects the default development configuration when these
variables are absent, even when reusing a previously configured build directory.
With direct IDF builds, pass the corresponding CMake `-D` options explicitly;
CMake cache values persist until changed.

Only the public key enters the firmware. `TOS_APP_SIGNING_KEY` selects the raw
32-byte private seed consumed by `hb_pack.py`; its default is the existing dev
seed. Production bundles must be signed using the identity selected for that
firmware. Existing development-signed bundles will fail verification against a
production-only trust store. No key rotation or multi-key policy is implied.

The old `gen_key.py` is a development-only helper that **overwrites** the dev seed
and checked-in trust header. Do not use it for production provisioning. Generate
and retain the real production identity outside this repository; supply only its
public half to the firmware build. This work produces app-only candidates; use
[the reviewed High Boy deployment boundary](../highboy/README.md#flash-and-storage-boundary),
not generic flashing instructions.

---

## 7. Install, run, manage

Apps live only in `/sdcard/apps/*.hb` — the firmware ships none.

- **Manual:** copy `myapp.hb` to `/sdcard/apps/myapp.hb` (card reader / USB-MSC).
- **Companion:** `FILE_WRITE` the bundle to `/sdcard/apps/<name>.hb`, then
  `APP_INSTALL <name>` to verify the signature (see
  [`host_link/app-commands.md`](../host_link/app-commands.md)).

### Run

- **Console:** `apprun <name>`. First run needs consent: `apprun <name> grant`
  approves the app's capabilities and runs it; later runs go straight through.
- **UI:** menu -> DEV -> Apps -> pick -> OK -> Allow. Consent persists (shared with
  the console).
- **Companion:** `APP_GRANT <name>` then `APP_START <name>`.

### Manage (console)

| Command | Does |
|---------|------|
| `apps` | list running apps |
| `appstop [name]` | stop one (or the only one if unnamed) |
| `appgrant` | list stored capability grants |
| `appgrant <name> revoke` | revoke a grant (re-prompts next run) |

Up to `TOS_APP_MAX` (4) apps run concurrently.

---

## 8. Constraints and gotchas

- **No libc, no firmware headers.** Only `tos_api.h` and the `api->` calls.
- **Writable globals are fine.** `.data`/`.bss` are loaded into a separate R/W
  region; the loader fixes up cross-region references. Ordinary C works.
- **App code runs from PSRAM**, not internal RAM; the firmware and its render path
  stay in flash, so a busy app slows nothing but shares PSRAM/L2 bandwidth.
- **Signed-only.** An unsigned, tampered, or untrusted-key bundle is refused
  before it loads.
- **Capabilities gate, they don't crash.** An ungranted call returns an error.
- **Name is the identity.** Grants and `appstop`/`apps` key on the app name (the
  `.hb` base filename == the `--name` you packed). Keep it unique and <= 15 chars.
- **Per-app arena is 1 MB.** `mem_alloc` past that returns NULL; everything is
  reclaimed when the app exits.
- **Not a sandbox (yet).** Native code is trusted-by-signature; memory isolation
  (PMP W^X) and Secure Boot are the remaining hardening step (Phase 10).

---

## 9. Extending the ABI (for firmware devs)

The API only lets apps do what the firmware **exposes**. Exposing more is done in
the firmware, on purpose, as typed curated functions — never by handing apps the
internal command table. The pattern (WiFi is the worked example):

1. **Declare the surface** in `tos_api.h`: a `tos_<sys>_api_t` sub-struct of
   function pointers, and a `const tos_<sys>_api_t *<sys>;` pointer in `tos_api_t`.
   Use self-contained types (no firmware/IDF structs leak into the ABI). Bump
   `TOS_ABI_VERSION_MINOR` (additive).
2. **Implement wrappers** in `tos_api_<sys>.c`: each function checks a capability
   (`if (!tos_app_cap_check(TOS_CAP_...)) return ESP_ERR_NOT_SUPPORTED;`), then
   calls the real firmware (a service function, or `spi_bridge_send_command` to
   the C5), converting to/from the ABI's typed args. Define the
   `const tos_<sys>_api_t tos_<sys>_api_impl = { ... }` table.
3. **Wire it** in `tos_api.c`: `extern` the impl and set `.<sys> = &tos_<sys>_api_impl`.
4. **Document** the calls + their capabilities here.

`tos_api_wifi.c` is the template: connectivity wraps `wifi_service_*`; the
monitors/attacks wrap `spi_bridge_send_command(SPI_ID_WIFI_*, …)`. Each new area
(ir, subghz, ble) follows the same four steps.

---

## 10. Example apps

In [`tools/app_sdk/`](../../tools/app_sdk), each is one `app.c` you can copy:

| App | Shows |
|-----|-------|
| `hello` | minimal fire-and-forget app |
| `blink` | a daemon loop with `should_stop` |
| `counter` | writable globals + a `.data` pointer array |
| `alloc` | the memory arena (allocates, leaks, gets reclaimed) |
| `svc` | registers a console command, runs until stopped |
| `spin` | a misbehaving app (ignores stop) — exercises force-kill |
| `wifiscan` | the WiFi subsystem: `api->wifi->scan()` + typed results |
| `rfread` | ABI 1.10 NFC discovery and native LF RFID capture, with receive permission |

## Scripts and native apps

Developer → Scripts runs plain JavaScript and Lua files through the
[script interpreters](../scripts/README.md). Developer → Apps continues to run
signed `.hb` bundles for richer native applications and custom UI. The script
screen is separate from the Apps launcher and does not package script files as
`.hb` apps.

Scripts use the same host table and capability checks through `tos.call(...)`.
Their permission approval is per run, and their adapter exposes scalar values,
records and opaque handles rather than native pointers or C callbacks. See the
[script ABI reference](../scripts/README.md#shared-firmware-abi) for bindings and
limits. Native `.hb` packaging, signatures and binary ABI remain unchanged.

The internal task context supports synchronized stop/resource-loss callbacks and
an optional return-screen ID for interpreted callers. Native apps keep their
existing flags and Apps return screen. Binding uses a statically initialized
lock; if its fixed table is full, both native and script runners refuse execution
rather than running with the privileges of an unbound firmware task.

## Native NFC/RFID API (ABI 1.10)

`api->nfc->poll(&tag, timeout_ms)` performs one A/B/F/V discovery pass. Timeout
is 1–2000 ms and is checked between native probes; an in-flight probe may finish
after that deadline. It returns `ESP_ERR_NOT_FOUND` when no tag was found.
`tag.protocol` uses `tos_nfc_proto_t`, including appended `TOS_NFC_FELICA`;
UID, ATQA and SAK are copied from the native result. NFC block read/write and
emulation/session fields remain NULL: check optional function pointers before
calling them. Their on-device screens use separate native services.

`api->rfid->read(&tag, timeout_ms)` captures EM4100/EM4102 with a 40–500 ms sampling
window. `id` is ten uppercase hex characters, `raw_value` is the 40-bit payload,
`bit_count` is 40 and `card_number` is its low 32 bits. No facility interpretation
is inferred (`facility_code=0`). No write or clone operation is published.

Both require approved `TOS_CAP_RADIO_RX`, check stop before/after the broker lock,
and execute on the persistent firmware RF worker, sharing native driver, antenna
reservation and power-lock cleanup with the [companion RF backend](../host_link/rf-commands.md).
The caller may wait up to 3 seconds for the local-call lock. Antenna contention
returns `ESP_ERR_INVALID_STATE`; hardware/setup failures propagate as errors.
A new local read retries pending worker-owned cleanup; failed OFF retains the
reservation and power hold. Local calls do not authenticate a companion session,
consume its counters or emit a USB/BLE response. They do not require HOSTLINK.

Native force-kill waits for the local-call gate before deleting a task. If the
gate or SPI/UI locks cannot be obtained, it returns a timeout. Natural app exit
retries cleanup after a failed kill claim instead of remaining suspended forever.
The native ABI appends pointers after `fs`, preserving older field offsets.
SDK `build.sh` now signs bundles with the ABI version of the header it compiled
against; manual packers must specify `--abi-minor 10` for these calls.
JavaScript and Lua expose the same calls through `tos.call("nfc.poll", 500)` and
`tos.call("rfid.read", 500)`, with per-run receive permission and typed records.
Sub-GHz and LoRa tables remain unpublished pending their ownership adapters.

## Native IR API (ABI 1.11)

`api->ir->receive(&code, timeout_ms)` captures a decoded remote code with
`TOS_CAP_RADIO_RX` and a 1–1000 ms timeout. `api->ir->send(&code)` sends one
decoded code with `TOS_CAP_RADIO_TX`. The table is appended after RFID, preserving
all older table offsets. Protocol values use `tos_ir_proto_t` (NEC=0 through
NEC42=11); the adapter maps to the native decoder's UNKNOWN-prefixed enum.
The code carries protocol, address, command and repeat. Extended NEC and explicit
Sony bit-count selection are not represented by this API. Raw, file and AC send
pointers remain NULL; callers must check optional pointers.

Operations check capability and cancellation before entering the local-call lock
and again after it. Hardware executes on the persistent firmware worker, so
force-killing an app waits for the same gate and cannot strand a task-owned IR
channel. A native UI/companion operation or torch blocks app acquisition. Failed
hardware teardown retains ownership; the next local IR operation retries it.
RX timeout and decode-not-found errors propagate distinctly. Local operations
never authenticate a companion or emit a transport response. See [IR ownership](../ir/README.md#app-worker-lease).

Scripts call the same table with `tos.call("ir.receive", 500)` or
`tos.call("ir.send", 0, address, command, false)`. Receive returns
`{protocol, address, command, repeat}`. Both bindings require ABI 1.11 and their
per-run RX/TX approval. Manually packed apps using IR require `--abi-minor 11`;
the SDK build script already reads that requirement from its compiled header.
