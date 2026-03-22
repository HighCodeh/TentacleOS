# Contributing to High Boy Firmware

Thank you for your interest in contributing to High Boy! We welcome contributions from everyone. This document provides guidelines and instructions to help you get started.


## Getting Started

### 1. Cloning the Repository
Clone the repository and its submodules recursively to ensure you have all dependencies:

```bash
git clone --recursive https://github.com/HighCodeh/TentacleOS.git
cd TentacleOS
```

### 2. Prerequisites
This project is built using the **Espressif IoT Development Framework (ESP-IDF)**.

- **Required Version:** ESP-IDF v5.3 or later (Check the latest stable version).
- **Setup Guide:** Follow the [Official ESP-IDF Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html).

Ensure your environment is set up:
```bash
. $HOME/esp/esp-idf/export.sh
```

### 3. Compiling
The project supports multiple hardware targets. Navigate to the specific firmware directory for your target:

#### ESP32-C5
```bash
cd firmware_c5
idf.py set-target esp32c5
idf.py build
```

#### ESP32-P4
```bash
cd firmware_p4
idf.py set-target esp32p4
idf.py build
```

*(Note: In the `/tools` directory, there is a `.sh` file responsible for automatic building and flashing.)*


## Development Workflow

### Coding Style & Conventions
We follow the [ESP-IDF Style Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/style-guide.html) with the following specific conventions:

#### 1. Naming Conventions
- **Files:** Use `snake_case.c` and `snake_case.h`.
- **Functions:** Use `snake_case` (e.g., `subghz_receiver_start`).
- **Variables:** Use `snake_case` (e.g., `decoded_data`).
- **Types (Structs/Enums):** Use `snake_case_t` suffix (e.g., `subghz_data_t`).
- **Macros & Constants:** Use `UPPER_SNAKE_CASE` (e.g., `RMT_RESOLUTION_HZ`).

#### 2. Code Structure
- **Indentation:** Use 4 spaces (preferred) or 2 spaces if consistent with the existing file.
- **Include Guards:** All headers must use `#ifndef FILENAME_H` guards.
- **C++ Compatibility:** Header files should include the `extern "C"` block for C++ compatibility.
- **License Header:** New files should include the Apache 2.0 license header at the top.

#### 3. Logging & Error Handling
- **Tags:** Define a `static const char *TAG = "MODULE_NAME";` at the top of `.c` files.
- **Logging:** Use `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`, and `ESP_LOGD` for system output.
- **Errors:** Use `ESP_ERROR_CHECK()` for critical initialization steps.

#### 4. Documentation
- Use Doxygen-style comments (`/** ... */`) in header files to document public APIs and structures.
- All comments and documentation must be in **English**.

---

### Commit Conventions
We use [Conventional Commits](https://www.conventionalcommits.org/). This is **enforced by a git hook** — commits that don't follow the format will be rejected.

Format: `<type>(<scope>): <description>`

| Type | Description | Version Impact |
|---|---|---|
| `feat` | New feature | Minor bump (0.**X**.0) |
| `fix` | Bug fix | Patch bump (0.0.**X**) |
| `perf` | Performance improvement | Patch bump (0.0.**X**) |
| `docs` | Documentation only | No bump |
| `style` | Code style (formatting, etc.) | No bump |
| `refactor` | Code refactoring | No bump |
| `test` | Adding or updating tests | No bump |
| `chore` | Maintenance tasks | No bump |
| `ci` | CI/CD changes | No bump |
| `build` | Build system changes | No bump |
| `revert` | Reverting a previous commit | No bump |

**Breaking changes** (major version bump) are indicated by adding `!` before the colon:
```
feat(spi)!: redesigned bridge protocol
fix!: changed public API return types
```

Examples:
```
feat(subghz): add waterfall visualization
fix(spi): corrected timeout on bridge init
feat(bt)!: redesigned BLE protocol
chore: cleanup unused imports
refactor(ota): simplified update flow
```

### Automatic Versioning
This project uses **Semantic Versioning** with fully automated version bumps via [semantic-release](https://github.com/semantic-release/semantic-release).

- Versioning is handled entirely by **GitHub Actions** — contributors do not need to manage versions manually.
- When a PR is merged into `main`, the CI pipeline analyzes commit messages, determines the version bump, creates a git tag, generates a changelog, and publishes a GitHub Release with the firmware binaries.
- Just write your commits following the Conventional Commits format and the rest is automatic.

### Git Hooks Setup
After cloning the repository, run the setup script to install the required git hooks:

```bash
./tools/setup.sh
```

This installs the `commit-msg` hook that validates your commit messages follow the Conventional Commits format. **Without this, your commits may be rejected during code review.**

---

## Continuous Integration (CI)
We use **GitHub Actions** to automatically build the project for all supported targets upon every push and Pull Request.

- **Status Checks:** All CI builds must pass before a Pull Request can be merged.
- **Blocked Merges:** If any build fails, the merge will be blocked until the issues are resolved.

---

## Pull Request Process

1. **Create a Branch:** Create a feature or bugfix branch from `dev` or `main`.
2. **Commit Changes:** Use conventional commit messages.
3. **Verify Build:** Ensure the project compiles locally for your target.
4. **Push & PR:** Push to your fork and create a Pull Request against the `dev` branch.
5. **CI Validation:** Wait for GitHub Actions to complete the build verification.
6. **Review:** At least one maintainer must review and approve your PR before merging.


## Code of Conduct
By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md).

## Communication
For questions or discussions, please use GitHub Issues or join our community channels ([Discord](https://discord.gg/high-code) / Telegram).
