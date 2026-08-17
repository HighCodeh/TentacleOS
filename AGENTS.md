# TentacleOS — Agent Instructions

## Project Overview

Embedded firmware for the High Boy platform (ESP32-S3/P4/C5). Dual-firmware architecture: ESP32-C5 is the co-processor, ESP32-P4 is the master that embeds the C5 binary at build time.

## Build & Flash

ESP-IDF v5.5.3 is the pinned version. Source the environment before any build command:

```bash
. $HOME/esp/v5.5.3/esp-idf/export.sh   # or rely on $IDF_PATH
```

Full build (C5 then P4, in order — P4 embeds the C5 binary):

```bash
./tools/build.sh
```

Build a single target:

```bash
cd firmware_c5 && idf.py -DIDF_TARGET=esp32c5 build
cd firmware_p4 && idf.py -DIDF_TARGET=esp32p4 build
```

Flash (P4 only, which also programs C5):

```bash
./tools/flash.sh
```

## Formatting

Formatting is enforced by CI and the pre-commit hook. Run manually before committing:

```bash
./tools/format.sh                            # fix all firmware sources
./tools/format.sh --check                    # verify all firmware sources
./tools/format.sh --changed <base-ref>        # fix files changed from a base
./tools/format.sh --check-changed <base-ref>  # verify changed files (CI uses this)
```

Config: `.clang-format` — LLVM base, 2-space indent, 100-column limit, pointers right-aligned, no sorted includes. Only `.c` and `.h` files under `firmware_*/` are formatted.

## Commit Messages

Conventional Commits enforced by git hook and CI. Format: `<type>(<scope>): <description>`

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `chore`, `ci`, `build`, `revert`, `delete`, `deleted`, `remove`, `removed`

Breaking changes use `!` before the colon. Header max length: 200 chars.

## Repo Structure

```
firmware_c5/          # ESP32-C5 co-processor firmware
firmware_p4/          # ESP32-P4 master firmware (embeds C5 binary)
  components/
    Drivers/          # Hardware drivers (SPI, buttons, display, radio, USB)
    Service/          # Support services (OTA, WiFi, console, storage, SPI bridge)
    Core/             # System core and main managers
    Applications/     # User-facing apps (UI, bad_usb, SubGhz)
    Drivers/spi_bridge_phy/  # SPI bridge physical layer (P4 side)
  main/main.c         # Entry point
common/metadata/      # Shared metadata
tools/                # Build, flash, format, setup scripts
```

Each target is a standalone ESP-IDF project with its own `CMakeLists.txt`, `partitions.csv`, and `sdkconfig.defaults`.

## Coding Standards

All rules are in `CODING_STANDARDS.md`. Key points an agent must not miss:

- **Public functions** are prefixed with module name (`cc1101_set_frequency()`). **Static functions** drop the prefix (`process_pulse()`).
- **Variables**: local `snake_case`, static `s_` prefix, global `g_` prefix, bools `is_`/`has_`/`can_`, output params `out_`.
- **Types**: `module_name_t` with `_cb_t` for callbacks.
- **Constants**: `UPPER_SNAKE_CASE`. Every literal with domain meaning must be a named `#define` (except `0`, `1`, `NULL`, `true`, `false`).
- **Enums**: `UPPER_SNAKE_CASE` with module prefix, include `_COUNT` sentinel when iterable.
- **Fixed-width types** from `<stdint.h>` for all hardware code. Never rely on implicit `int`.
- **Error handling**: public functions return `esp_err_t`. Every `malloc` must be checked for `NULL`, logged with `ESP_LOGE`, and handled via `goto cleanup`.
- **Logging**: every `.c` file defines `static const char *TAG = "MODULE_NAME";`. Use `ESP_LOGx` macros, never `printf`.
- **File layout order**: license, own header, C stdlib, ESP-IDF/FreeRTOS, project headers, defines, static types, static vars, forward decls, public funcs, static funcs.
- **Headers**: `#ifndef` guards, `extern "C"` blocks, include order separated by blank lines.
- **Source file layout**: `^[0-9a-z_]+\.[ch]$`, filename is a prefix for its content.

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
