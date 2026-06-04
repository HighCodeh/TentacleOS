#!/usr/bin/env bash
# Build and run the HID layouts equivalence test.
# Usage: ./run_equiv.sh            (uses cc, or $CC if set)
set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-cc}"
NEW_SRC="../../firmware_p4/components/Applications/bad_usb/hid_layouts.c"

echo "Compiling with: $CC"
"$CC" -std=c11 -Wall -Wno-unused-variable -I shims \
  equiv_test.c hid_layouts_reference_old.c "$NEW_SRC" \
  -o equiv_test

echo "Running:"
./equiv_test
