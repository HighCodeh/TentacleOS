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

| Tool | Version | Purpose |
|------|---------|---------|
| [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) | v5.3+ | Build system and SDK |
| [clang-format](https://clang.llvm.org/docs/ClangFormat.html) | any | Code formatting (enforced by pre-commit hook) |
| Git | 2.9+ | Required for `core.hooksPath` support |

Install ESP-IDF and ensure your environment is set up:
```bash
. $HOME/esp/esp-idf/export.sh
```

Install clang-format:
```bash
# Arch / Manjaro
sudo pacman -S clang

# Ubuntu / Debian
sudo apt install clang-format

# macOS
brew install clang-format
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
All coding rules, naming conventions, formatting, error handling and reference examples are in [CODING_STANDARDS.md](CODING_STANDARDS.md). Read it before submitting code.

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

This configures git to use the project hooks (`.githooks/`). The hooks include:
- **pre-commit** — checks code formatting with `clang-format`
- **commit-msg** — validates Conventional Commits format

## Continuous Integration (CI)
We use **GitHub Actions** to automatically build the project for all supported targets upon every push and Pull Request.

- **Status Checks:** All CI builds must pass before a Pull Request can be merged.
- **Blocked Merges:** If any build fails, the merge will be blocked until the issues are resolved.

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
