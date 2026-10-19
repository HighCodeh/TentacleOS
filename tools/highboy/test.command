#!/bin/bash
# Public-source host tests only; no device or account access.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$repo/tools/highboy/check_contracts.py"
python3 "$repo/tools/highboy/test_contracts.py"
python3 "$repo/tools/highboy/test_subghz.py"
python3 "$repo/tools/highboy/test_rf_ownership.py"
python3 "$repo/tools/highboy/test_nfc_screen_owners.py"
python3 "$repo/tools/highboy/test_ir_library.py"
python3 "$repo/tools/scripts/generate_js_stdlib.py" --check
python3 "$repo/tools/scripts/test_engine.py"
python3 "$repo/tools/scripts/test_runner.py"
python3 "$repo/tools/highboy/test_muse_native.py"
python3 "$repo/tools/highboy/test_preservation.py"
node --test "$repo/tools/highboy/tests/test_usb_msc.mjs" \
  "$repo/tools/highboy/tests/test_doom_dma.mjs"
