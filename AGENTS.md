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

# TentacleOS — Agent Instructions

## Local integrated branch exception

This checkout includes the verified High Boy Muse integration. Read
`docs/highboy/README.md` before changing build, protocol or deployment paths.
`tools/build.sh` routes to an offline app-only build: P4 uses IDF v5.5.3, C5 uses
IDF v6.0.1 for its Muse integration. Preserve the paired protocol v8 headers and
the C5 memory overlay. Do not run older external `prepare.py` generators here.
`tools/flash.sh p4|c5 -p PORT` performs a guarded single-chip app-only update: full flash backup,
matching installed partition map and valid existing boot/app images are required.
Use `--check` for backup and validation without writing. It does not update the
other chip or restore assets. Full restoration/migration remains gated; upstream's generic flash
commands below are not valid for this local candidate.

## Project Overview

The device this firmware runs on is named **Highboy** or **High Boy**; both names refer to the same device.

Embedded firmware for the High Boy platform (ESP32-S3/P4/C5). Dual-firmware architecture: ESP32-C5 is the co-processor and ESP32-P4 is the master. This integrated profile builds separate applications; flashing P4 does not program C5.

## Build & Flash

ESP-IDF v5.5.3 is the pinned version. Source the environment before any build command:

```bash
. $HOME/esp/v5.5.3/esp-idf/export.sh   # or rely on $IDF_PATH
```

Full paired build (C5 then P4):

```bash
./tools/build.sh
```

Build a single target:

```bash
cd firmware_c5 && idf.py -DIDF_TARGET=esp32c5 build
cd firmware_p4 && idf.py -DIDF_TARGET=esp32p4 build
```

Update an installed P4 (C5 is not programmed):

```bash
./tools/flash.sh p4 -p /dev/your-p4-port
./tools/flash.sh c5 -p /dev/your-c5-port
```

## Formatting

Formatting is handled automatically by the `.clang-format` pre-commit hook — as an agent, do **not** run it manually. CI verifies it, and `./tools/format.sh` exists for humans who want to run it themselves:

```bash
./tools/format.sh                            # fix all firmware sources
./tools/format.sh --check                    # verify all firmware sources
./tools/format.sh --changed <base-ref>        # fix files changed from a base
./tools/format.sh --check-changed <base-ref>  # verify changed files (CI uses this)
```

Config: `.clang-format` — LLVM base, 2-space indent, 100-column limit, pointers right-aligned, no sorted includes. Only `.c` and `.h` files under `firmware_*/` are formatted.

## Commit Messages

Conventional Commits enforced by git hook and CI. Format: `<type>(<scope>): <description>`

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `chore`, `ci`, `build`, `revert`, `delete`, `deleted`, `remove`, `removed`, `diag`, `tweak`

Breaking changes use `!` before the colon. Header max length: 200 chars.

## Repo Structure

```
firmware_c5/          # ESP32-C5 co-processor firmware
firmware_p4/          # ESP32-P4 master firmware (embeds C5 binary)
  components/
    Drivers/          # Hardware drivers (SPI, buttons, display, radio, USB)
    Service/          # Support services (OTA, WiFi, console, storage, SPI bridge)
    Core/             # System core and main managers
    Applications/     # User-facing apps (UI, bad_usb, SubGhz, gameboy, nfc)
    Drivers/spi_bridge_phy/  # SPI bridge physical layer (P4 side)
  main/main.c         # Entry point
common/metadata/      # Shared metadata
tools/                # Build, flash, format, setup scripts
```

Each target is a standalone ESP-IDF project with its own `CMakeLists.txt`, `partitions.csv`, and `sdkconfig.defaults`.

**Component placement:** every firmware component lives under one of the four
architecture roots — `Applications/`, `Core/`, `Drivers/`, `Service/`. Do **not**
add a new top-level component directly in `components/`. When a piece of code must
be its own component (to break a dependency cycle, or to override an IDF component),
nest it under the matching root and register it via `EXTRA_COMPONENT_DIRS` in the
project `CMakeLists.txt`. Existing examples: `Applications/gameboy`,
`Applications/doom`, `Service/fatfs` (vendored IDF override), `Core/resource_mgr`.

## Documentation

Project documentation lives in `docs/` — one directory per component, indexed in
[docs/README.md](./docs/README.md). When you need to understand a subsystem, a
decision, or the project, consult its `docs/<component>/README.md` first (each
component's in-tree README points back to it) instead of guessing from the code.
Always keep it up to date: when you change a subsystem's behavior, contract, or
interface, update its doc in the same change.

## License headers — mandatory for every file

Every first-party file you create or modify MUST carry the project's license
notice at the top. This applies to source files, headers, scripts, tests, build
files, CI workflows, configuration files and documentation, not only `.c`/`.h`.
Use the existing TentacleOS GPL-3.0-or-later notice and include
`SPDX-License-Identifier: GPL-3.0-or-later`; do not copy a different license from
an illustrative example or invent copyright holders.

Use the format's native comments (`//`, `#`, or HTML comments in Markdown).
A required shebang or format declaration stays first; the license follows
immediately. For JSON, binaries or other formats that cannot contain comments,
place the notice in an adjacent `<filename>.license` file instead of breaking
the format. Generated artifacts must receive notices through their generator,
never through manual edits. Preserve third-party copyright/license notices and
their actual licenses; do not replace them with the project's license. Do not
alter the text of existing license documents such as `LICENSE`.

Check these notices before finishing a change. Missing license headers are a
project standards violation, including in scripts and test fixtures.

## Coding Standards

All code MUST follow the conventions defined in [CODING_STANDARDS.md](./CODING_STANDARDS.md).
Read it before writing any code. It covers naming, types, error handling, concurrency,
file organization, logging, and header structure.

Key points an agent must not miss:

- **Public functions** are prefixed with module name (`cc1101_set_frequency()`). **Static functions** drop the prefix (`process_pulse()`).
- **Variables**: local `snake_case`, static `s_` prefix, global `g_` prefix, bools `is_`/`has_`/`can_`, output params `out_`.
- **Types**: all types use `snake_case` with `_t` suffix, prefixed by the module name (`module_name_t`), with `_cb_t` for callbacks.
- **Constants**: `UPPER_SNAKE_CASE`. Every literal with domain meaning must be a named `#define` (except `0`, `1`, `NULL`, `true`, `false`).
- **Enums**: `UPPER_SNAKE_CASE` with module prefix, include `_COUNT` sentinel when iterable.
- **Fixed-width types** from `<stdint.h>` for all hardware code. Never rely on implicit `int`.
- **Error handling**: public functions return `esp_err_t` when they can fail. Every `malloc` must be checked for `NULL`, logged with `ESP_LOGE`, and handled via `goto cleanup`. Use `goto cleanup` for functions with multiple resources.
- **Logging**: every `.c` file defines `static const char *TAG = "MODULE_NAME";` at the top. Use `ESP_LOGx` macros, never `printf`.
- **File layout order**: license, own header, C stdlib, ESP-IDF/FreeRTOS, project headers, defines, static types, static vars, forward decls, public funcs, static funcs.
- **Headers**: `#ifndef` guards, `extern "C"` blocks, include order separated by blank lines.
- **Source file layout**: `^[0-9a-z_]+\.[ch]$`, filename is a prefix for its content.
- **Comments**: only a short, pertinent WHY when it is non-obvious. No multi-line rationale dumps, no restating what the code does, no banner/dashed-line section separators.
- **Task priority and core** come from `sys_prio.h` (`Drivers/sys_prio/include`); always pin with `xTaskCreatePinnedToCore`. UI/render on core 1, radios on core 0. See Concurrency in `CODING_STANDARDS.md`.
- **`spi_protocol.h` has two hand-maintained copies** (`firmware_p4` and `firmware_c5`). Any change to one (ops, structs, version) MUST be mirrored to the other in the same commit: they are one shared contract, kept aligned by the runtime version check. Even P4-local ops are reserved in both to avoid future collisions.
- **Only change what was explicitly asked for** — do not add unsolicited refactors or improvements.

## Hook Setup

Run once after cloning:

```bash
./tools/setup.sh
```

This sets `core.hooksPath` to `.githooks/` (pre-commit: clang-format, commit-msg: conventional commits).

## CI

GitHub Actions runs on PRs to `main`/`dev`:
1. **Format check** — `./tools/format.sh --check`
2. **Commitlint** — validates all commits in PR
3. **Build** — both targets inside `espressif/idf:v5.5.3` container via `./tools/build.sh`

All must pass before merge.

## Generated Files (do not edit)

- `managed_components/` — ESP-IDF component manager (gitignored)
- `sdkconfig` / `sdkconfig.old` — ESP-IDF build config (gitignored); use `sdkconfig.defaults` instead
- `dependencies.lock` — ESP-IDF component manager lockfile (gitignored)
- Build outputs in `build/` directories (gitignored)

## Versioning

Automated via semantic-release on `main`. Do not manage versions manually — commit messages determine bumps. Version is written to:
- `firmware_p4/assets/config/OTA/firmware.json`
- `firmware_c5/assets/config/OTA/firmware.json`
- `firmware_p4/components/Service/ota/include/ota_version.h`
