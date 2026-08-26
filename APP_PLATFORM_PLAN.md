# TentacleOS App Platform - Native (.elf) App Loader Plan

**Status: phases 1-7 IMPLEMENTED and validated on hardware.** Phase 8 (packaged
PC SDK) is deferred; Phase 10 (isolation hardening: PMP W^X + Secure Boot) is the
remaining **TODO**. The platform takes TentacleOS from "every app is compiled into
the firmware" to "compile an app on a PC, drop a signed bundle onto the device,
and it runs without reflashing." The runtime is a **native relocatable ELF loaded
from SD**, called through a **stable versioned host API**, gated by
**capabilities**, and authenticated by an **Ed25519 signature** on the bundle.

App developer guide: **[`docs/app_sdk/README.md`](docs/app_sdk/README.md)** (how
to write, sign, install, and run an app). Companion protocol:
`docs/host_link/app-commands.md`. This document is the design/source-of-truth for
the platform contract; §12 tracks per-phase status.

Related:
- `docs/host_link/protocol.md` - the companion wire protocol (reused for app install/transfer)
- `docs/spi_bridge/README.md` - the `spi_id_t` command table (single source of truth)
- `docs/storage_api/README.md` - SD layout (`/sdcard/apps`, `/sdcard/apps_data` already scaffolded)
- `CODING_STANDARDS.md` - conventions all new code follows

---

## 0. Where we are today (baseline)

Verified against the tree so the plan starts from facts, not assumptions:

- **No runtime code loading of any kind.** No ELF loader, no VM/interpreter, no
  manifest reader. Apps are C components under
  `firmware_p4/components/Applications/` linked into the image; the launcher menu
  is a static enum (`ui/include/ui_manager.h`, `screen_id_t`).
- **Scaffolding exists, loader does not.** `TOS_PATH_APPS = /sdcard/apps` and
  `TOS_PATH_APPS_DATA = /sdcard/apps_data` are created on first boot
  (`tos_first_boot.c`) and wiped on factory reset (`tos_factory_reset.c`).
  `docs/storage_api/README.md` even calls `apps/` "External apps (.hb)". Nothing
  consumes them.
- **Dispatch is a hardcoded ladder.** `host_link.c:230-338` is 12 `*_is_op()` /
  `*_handle()` branches with a uniform handler signature. The console side is
  already a registry (`esp_console_cmd_register`). Both need to accept runtime
  add/remove for apps.
- **Target:** `esp32p4` (RISC-V). **PSRAM enabled** (HEX, 200 MHz) but
  `SPIRAM_XIP_FROM_PSRAM` is **off** (code is not fetched from PSRAM today).
  **PMP is already in use** for the IDRAM split (`CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT=y`).
- **Secure Boot v2 is "preferred" but not enabled**; flash encryption is off.
  `libsodium` is already a P4 dependency (usable for bundle signatures now).
- **Flash is full.** OTA A/B slots ~2.94 MB each (~434 KB headroom), `assets`
  LittleFS ~2 MB near-full, `coredump` 64 KB. There is **no** free flash
  partition for apps: apps live on the **SD card**.

---

## 1. Goals / non-goals

**Goals**
- Load and run a native app compiled on a PC from `/sdcard/apps/<id>.hb` with no reflash.
- A **stable, versioned ABI** so an app built today keeps running across firmware updates within the same major ABI.
- **Capability-gated** access to device functions (radio TX, HID, FS, etc.), not all-or-nothing.
- **Signed bundles**; the loader refuses unsigned or untrusted apps.
- Apps **register** their own host_link and console commands at load, unregister at unload.
- A crashing app is **contained**: it must not take down the OS, the render beat, or the radios.
- A **PC SDK**: linker script, headers, packaging + signing tool, and example apps.

**Non-goals (v1)**
- Strong memory isolation of untrusted code. Native ELF is **semi-trusted by
  design**: the signature is the real trust boundary, capabilities limit blast
  radius, PMP hardening is a later phase (§10). We do not claim a sandbox we do
  not have.
- Multiple apps running concurrently. v1 runs **one** foreground app at a time.
- Hot ABI upgrades of a running app. ABI is checked at load; mismatch is refused.
- On-device compilation. Toolchain stays on the PC.

---

## 2. Threat model (be honest about native code)

A native ELF has the same instruction set and, without a full MMU sandbox, the
same reach as the firmware. Our containment is layered, not absolute:

1. **Provenance:** the bundle is Ed25519-signed (`libsodium`). The device holds a
   trust store of public keys (HIGH CODE key + optionally per-customer keys). An
   app that does not verify is never mapped. This is the primary gate.
2. **Capabilities:** the manifest declares required capabilities; the loader
   grants only those; every host_link/console/API entry checks the grant before
   acting. A signed app still cannot TX on a radio it did not request.
3. **Supervision:** the app runs as a pinned task watched by `sys_monitor` and
   the TWDT (panic-on-timeout policy already in force). A hang or a fault is
   caught and the app is aborted, not the OS.
4. **Hardening (phased):** heap arena accounting, then PMP-based W^X and region
   limits on the app once the ABI is stable (§10).

Consequence: v1 is appropriate for **apps you or your customers sign**, not for
arbitrary anonymous binaries. That matches the "SDK for our clients" goal.

---

## 3. Architecture

```
   PC (SDK)                         Device (P4)
 ┌───────────┐   .hb over        ┌──────────────────────────────────────┐
 │ app.c     │   host_link  ──►   │ /sdcard/apps/<id>.hb                 │
 │ tos_api.h │   FILE_WRITE       │   │ verify sig + manifest (tos_bundle) │
 │ link.ld   │   or SD copy       │   ▼                                    │
 │ hb-pack   │                   │ tos_elf_load ──► reloc into app arena  │
 └───────────┘                    │   │                                    │
                                  │   ▼ app_main(const tos_api_t*, argc,…) │
                                  │ app calls ONLY through tos_api_t:      │
                                  │   api->host_link_register(...)         │
                                  │   api->console_register(...)           │
                                  │   api->wifi_scan(...) / api->gpio(...)  │
                                  │   (each entry: capability check)       │
                                  │ supervised task (sys_prio + sys_monitor)│
                                  └──────────────────────────────────────┘
```

The keystone is that the app links against **nothing in the firmware**. It
receives one pointer, `const tos_api_t *api`, at entry and reaches every device
function through it. No symbol resolution against firmware internals, so the ABI
is a single reviewed struct we version deliberately, not the accidental sum of
every exported symbol.

---

## 4. The stable ABI (`tos_api_t`) - the contract

A single header, shipped in the SDK and compiled into the firmware, is the whole
contract between apps and the OS.

```c
// tos_api.h  (SDK + firmware, single source)
#define TOS_ABI_VERSION_MAJOR 1   // breaking; app refuses to load on mismatch
#define TOS_ABI_VERSION_MINOR 0   // additive; app may require <= firmware minor

typedef struct tos_api tos_api_t;

// Capability bits the app requests in its manifest and the API enforces.
enum {
  TOS_CAP_FS_READ    = 1u << 0,
  TOS_CAP_FS_WRITE   = 1u << 1,
  TOS_CAP_RADIO_TX   = 1u << 2,   // WiFi/BT/SubGHz/LoRa/IR transmit
  TOS_CAP_RADIO_RX   = 1u << 3,
  TOS_CAP_HID        = 1u << 4,   // BadUSB / HID injection
  TOS_CAP_UI         = 1u << 5,   // draw to the screen / LVGL surface
  TOS_CAP_HOSTLINK   = 1u << 6,   // register host_link commands
  TOS_CAP_CONSOLE    = 1u << 7,   // register console commands
  // ...
};

struct tos_api {
  uint32_t abi_major;
  uint32_t abi_minor;

  // lifecycle / os
  void   (*log)(int level, const char *tag, const char *fmt, ...);
  void  *(*malloc)(size_t n);        // charged to the app arena
  void   (*free)(void *p);
  void   (*delay_ms)(uint32_t ms);   // yields; keeps the TWDT fed
  void   (*yield)(void);

  // command surfaces (the registry from the dispatch refactor, §6)
  esp_err_t (*host_link_register)(const host_link_handler_t *h);
  esp_err_t (*host_link_unregister)(uint8_t category, uint8_t op_min);
  esp_err_t (*console_register)(const esp_console_cmd_t *cmd);

  // capability-gated device surfaces (grow additively, minor-bumped)
  esp_err_t (*fs_open)(const char *path, int flags, int *out_fd);
  // ... wifi_scan, ble_adv, subghz_tx, ir_tx, led_set, gpio, audio_tone, ui_* ...
};
```

Rules that keep it stable:
- **Never reorder or remove** a field within a major version. New calls are
  **appended** and bump the minor.
- Every capability-gated pointer checks `app->granted_caps` first and returns
  `SPI_STATUS_UNSUPPORTED` / `ESP_ERR_NOT_SUPPORTED` if not granted.
- The struct is built once at boot (`tos_api_build()`), `const`, shared by all apps.
- The app's manifest declares `abi_major` + `min_abi_minor`; the loader refuses
  on major mismatch or if the firmware minor is lower.

This struct is the single thing code review must guard forever. Everything else
can change freely behind it.

---

## 5. Bundle format (`.hb`) + trust

A `.hb` is a small container so the manifest, signature, ELF, and assets travel
together and are verified as a unit.

```
hb header: magic "HBAP", header_len, flags
manifest  : CBOR/JSON - { id, name, version, abi_major, min_abi_minor,
                          caps_requested, entry, icon, sha256(payload) }
signature : Ed25519 over (header || manifest || payload), 64 B  (libsodium)
payload   : the relocatable ELF, then optional packed assets
```

Verification order (loader refuses at the first failure, nothing is mapped until
all pass): magic/version -> manifest parse -> `sha256(payload)` match ->
Ed25519 signature against a key in the trust store -> `abi_major` match ->
requested caps are all grantable.

Trust store: `libsodium` public keys in NVS namespace `apps`, seeded with the
HIGH CODE signing key; customer keys added via a signed provisioning step (reuse
the host_link pairing UX). Signing happens on the PC in the SDK tool
(`hb-pack --sign`); the private key never touches the device.

---

## 6. Dispatch registry refactor (prerequisite, ships first)

Turn the two dispatch surfaces into registries that accept **runtime**
add/remove, so a loaded app plugs in and a stopped app plugs out.

**host_link** (`firmware_p4/components/Service/host_link/host_link_dispatch.{c,h}`):

```c
typedef uint8_t (*host_link_handler_fn)(uint16_t cmd, const uint8_t *payload,
                                        uint16_t plen, uint8_t *out,
                                        uint16_t out_cap, uint16_t *out_len);
typedef struct {
  uint8_t              category;   // SPI_CAT_*, matched on (cmd >> 8)
  uint8_t              op_min, op_max;
  uint16_t             out_cap;
  uint8_t              flags;      // HOST_LINK_F_LOCAL (bypass relay cap)
  uint32_t             caps;       // capability required to invoke (0 for builtins)
  host_link_handler_fn handle;
  const char          *name;
} host_link_handler_t;

esp_err_t host_link_register(const host_link_handler_t *h);
esp_err_t host_link_unregister(uint8_t category, uint8_t op_min);
```

- `process_frame` becomes: find handler by `(category, op)` -> capability check
  -> call -> `send_resp`. Unmatched relays to the C5 as today.
- The 12 existing branches migrate to `host_link_register_builtins()` with
  identical behavior (the `HOST_LINK_F_LOCAL` flag replaces the position-in-ladder
  cap logic). Shared serialized scratch buffer sized to the largest `out_cap`.
- Table is an array + lock (runtime mutation), not a `const` linker section.
- **Reserve category range `0x40-0x7F` for apps** (builtins use `0x00-0x0E`, `0xFF`).

**console:** already `esp_console_cmd_register`; add the runtime unregister path
(`esp_console_cmd_deregister`) on app unload and route both through the API struct
so app registrations can be tracked and torn down as a set.

This phase is independently valuable (it fixes the extensibility/versioning gaps
in the current host_link) and is a hard prerequisite for the loader.

---

## 7. Storage & lifecycle

**Layout (already scaffolded, formalize it):**
- `/sdcard/apps/<id>.hb` - installed bundles.
- `/sdcard/apps_data/<id>/` - per-app writable sandbox (path-jailed like host_link files).
- `/sdcard/apps/index.json` - installed list + versions (loader-maintained).

**Lifecycle:** `tos_app_manager`
`start(id)` verify -> load -> build per-app context (arena, granted caps) ->
`xTaskCreatePinnedToCore` at an **apps priority band added to `sys_prio.h`**
(below UI/render and radios; core 1 for UI-facing apps) -> register its beat with
`sys_monitor` -> call `app_main(api, argc, argv)`.
`stop(id)` signal cooperative stop -> `*_abort()` (never `vTaskDelete` per the
`sys_monitor` policy) -> unregister host_link/console handlers -> free arena ->
drop the beat.

**Supervision:** app task is watched by `sys_monitor`; TWDT panic policy already
in force means an app that stops yielding reboots the device, so `api->delay_ms`
/ `api->yield` must be the only sanctioned blocking primitives, and long loops
are documented to call them. A faulting app is caught and aborted; `sys_monitor`
escalates to controlled restart only if teardown itself wedges.

---

## 8. ELF loader core

`firmware_p4/components/Service/app_runtime/tos_elf.{c,h}`

- Input: the relocatable ELF from the verified `.hb` payload (in PSRAM).
- Parse ELF header + program/section headers; RISC-V only (`EM_RISCV`).
- Allocate the app image in the **app arena** (PSRAM), copy `PT_LOAD` segments,
  zero `.bss`, apply relocations (`R_RISCV_*`: `RELATIVE`, `CALL_PLT`, `HI20/LO12`,
  `GOT`), resolve the single imported symbol - the entry - and hand over
  `api` as the first argument. **No firmware symbols are imported.**
- **Execution region decision (open, blocking - see §11):** `.text` cannot run
  from PSRAM today (`SPIRAM_XIP_FROM_PSRAM` is off). Options to resolve in a spike:
  (a) enable instruction fetch from PSRAM for the app arena; (b) copy `.text` into
  a reserved internal-IRAM app region (scarce, hard size cap); (c) enable XIP and
  measure the icache cost to the UI. Pick before building past the parser.
- Evaluate the community `elf_loader` component vs. a purpose-built loader; the
  api-struct ABI means we need only relocation + one entry symbol, so a slim
  in-tree loader is likely simpler and auditable.

---

## 9. Install path (reuse host_link)

No new transport. The companion app installs over the existing authenticated
host_link:
- `FILE_WRITE` the `.hb` to `/sdcard/apps/<id>.hb` (already supported,
  path-sandboxed), then a new `SYSTEM` op `APP_INSTALL { id }` that verifies and
  registers it, plus `APP_LIST`, `APP_START`, `APP_STOP`, `APP_REMOVE`.
- Manual path: drop the `.hb` on the SD over USB-MSC / card reader; the loader
  picks it up on boot / on a rescan.
- UI: an "Apps" screen listing installed `.hb`s (replaces the current static
  games route), showing name/version/caps and a per-app start/stop, with a
  capability-consent prompt on first run.

---

## 10. Isolation hardening (TODO — the remaining phase)

Native code is not sandboxed by default; layer defenses once the platform works:
- **Arena accounting:** all `api->malloc` charged to a per-app budget; over-budget
  fails the alloc, not the heap.
- **PMP W^X and region bounds:** PMP is already used for the IDRAM split, so the
  hardware and IDF plumbing exist. Mark the app `.text` execute-only and its data
  no-execute, and fence the app arena so a wild write faults instead of
  corrupting OS state. Requires the execution-region decision (§8) first.
- **Cap enforcement audit:** verify every gated API entry actually checks
  `granted_caps` (a test that calls each with an empty grant and expects refusal).
- **Secure Boot v2 + flash encryption:** enable for the firmware itself
  (currently only "preferred"), so the trust chain under the app signature is real
  and the signing keys in NVS are protected. Coordinate with the partition/OTA
  layout (full-flash reflash required).

---

## 11. Open decisions / risks (resolve early)

1. **[BLOCKING] Where app `.text` executes** (§8). Gate the whole plan on a spike;
   it changes the loader, the linker script, and the RAM budget.
2. **ABI surface v1:** which device functions ship in `tos_api_t` first. Start
   minimal (log, mem, delay, host_link/console register, fs, led, one radio) and
   grow by minor bump.
3. **Bundle manifest encoding:** CBOR (compact, `cjson` is already in-tree for
   JSON but CBOR needs a dep) vs. JSON. Lean JSON for v1 to avoid a new dependency.
4. **Customer signing model:** one HIGH CODE key that co-signs, or per-customer
   keys in the trust store. Affects the provisioning UX and revocation.
5. **PSRAM budget:** app arena vs. LVGL/screen-share buffers (kept small pending
   the PSRAM prototype). Size the arena against real free PSRAM at boot.
6. **Two copies of the ABI header** (SDK + firmware) must not drift - generate
   both from one source, same discipline as the two `spi_protocol.h` copies.

---

## 12. Phased build order

Status legend: ✅ done (validated on hardware) · ◻️ TODO.

1. ✅ **Dispatch registry (host_link + console).** `host_link_dispatch.{c,h}`;
   runtime `host_link_register`/`unregister`; console uses `esp_console`.
2. ✅ **ABI (`tos_api_t`).** `Service/app_runtime/tos_api.{c,h}`; capability gate
   per call via a TaskHandle-keyed context (`tos_app_ctx`); `tos_api_get()`.
3. ✅ **Loader.** Two-alias PSRAM mapping (P4 forbids EXEC+WRITE in one mapping);
   ET_REL section loader in `tos_elf.c` handling writable data (code + rodata in
   an exec region, `.data/.bss` in a separate R/W region, per-symbol relocation).
4. ✅ **Lifecycle.** `tos_app_mgr` runs up to `TOS_APP_MAX` apps as supervised
   tasks; per-app memory arena (`tos_arena`, reclaims leaks); cooperative stop +
   force-kill; command/state teardown on exit; sys_monitor watches the tasks.
5. ✅ **Signed `.hb` bundle + trust store.** 40-byte header + ELF + 64-byte
   Ed25519 sig; `tos_hb.c` verifies against `tos_trust_keys.h` before load.
6. ✅ **Capability consent.** `tos_grants` (NVS, per app); `apprun <app> grant`
   and the Apps-UI consent modal; manager grants only `manifest & approved`.
7. ✅ **Install path + Apps UI.** `ui/screens/apps` launcher; companion ops
   `APP_LIST/INSTALL/START/STOP/REMOVE/GRANT` (`host_link_apps.c`,
   `docs/host_link/app-commands.md`); `appinstall` seeds the SD locally.
8. ◻️ **PC SDK packaging (deferred).** Working but dev-shaped in `tools/app_sdk/`
   (build.sh, hb_pack.py, gen_key.py, examples). Needs a clean self-contained
   package + the app developer guide (`docs/app_sdk/README.md`, written).
9. ◻️ **Isolation hardening (TODO).** PMP W^X on the app region, arena budget
   already enforced; **Secure Boot v2 + flash encryption** to make the signature
   chain real; force-kill of a wild write into a controlled fault.

---

## 13. PC-side SDK deliverables

- `tos_api.h` (the ABI) + `tos_app.h` (entry macro, manifest helpers), generated
  from the same source as the firmware copy.
- `app.ld` linker script producing a relocatable, position-independent RISC-V ELF
  with the expected single entry symbol and no firmware symbol imports.
- A minimal build wrapper (CMake or a `Makefile`) using the same RISC-V toolchain
  as IDF, so customers do not need a full ESP-IDF checkout.
- `hb-pack` tool: build manifest, hash payload, `--sign` with an Ed25519 key,
  emit `.hb`. Plus `hb-verify` mirroring the device checks for local testing.
- Example apps: "blink LED", "register a host_link command", "WiFi scan to
  screen", each exercising one capability.
- Docs: `docs/app_sdk/README.md` with the full ABI reference, capability list,
  bundle format, and the build+sign+install walkthrough.

---

## 14. What this reuses vs. builds new

**Reuses (already solid):** host_link transport + auth + file ops + install
channel; `spi_protocol.h` single-source command table; `sys_prio` bands;
`sys_monitor` supervision + `*_abort()` policy; TWDT panic discipline;
`/sdcard/apps` + `apps_data` scaffolding; `libsodium`; factory-reset wipe path;
the recovery/safe-mode boot combo (a bad app must be recoverable there).

**Builds new:** the dispatch registries (§6), the ABI struct (§4), the `.hb`
format + trust store (§5), the ELF loader (§8), the app manager/lifecycle (§7),
the app install ops + Apps UI (§9), the PMP hardening (§10), and the PC SDK (§13).
