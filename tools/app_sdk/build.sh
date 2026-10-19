#!/usr/bin/env bash
# Build each example app (app_sdk/<name>/app.c) into a signed <name>/app.hb.
# Copy the .hb to /sdcard/apps to install it. Requires the ESP-IDF env sourced.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$HERE/../../firmware_p4/components/Service"
MARCH=rv32imafc_zicsr_zifencei
MABI=ilp32f
# hb_pack needs PyNaCl, which lives in the system python, not the IDF venv.
PY="${SYSTEM_PYTHON:-/usr/bin/python3}"
SIGNING_KEY="${TOS_APP_SIGNING_KEY:-$HERE/keys/dev_ed25519.key}"
ABI_HEADER="$FW/app_runtime/include/tos_api.h"
ABI_MAJOR="$(awk '$2 == "TOS_ABI_VERSION_MAJOR" {print $3}' "$ABI_HEADER")"
ABI_MINOR="$(awk '$2 == "TOS_ABI_VERSION_MINOR" {print $3}' "$ABI_HEADER")"

for dir in "$HERE"/*/; do
  name="$(basename "$dir")"
  [ -f "$dir/app.c" ] || continue
  # ET_REL relocatable object: the split loader places code and writable data in
  # separate regions and patches every relocation, so no -fPIC / -shared.
  riscv32-esp-elf-gcc -march="$MARCH" -mabi="$MABI" \
    -Os -ffreestanding -fno-common -mno-relax -ffunction-sections -fdata-sections \
    -I"$FW/app_runtime/include" \
    -c -o "$dir/app.elf" "$dir/app.c"
  caps="$(cat "$dir/caps" 2>/dev/null || echo 0x20)" # per-app caps file, default UI
  "$PY" "$HERE/hb_pack.py" "$dir/app.elf" "$dir/app.hb" --name "$name" --caps "$caps" \
    --sign "$SIGNING_KEY" --abi-major "$ABI_MAJOR" --abi-minor "$ABI_MINOR"
  bytes="$(wc -c < "$dir/app.hb")"
  bytes="${bytes//[[:space:]]/}"
  echo "built '$name' -> $dir/app.hb ($bytes bytes)"
done

echo
echo "Copy an app.hb to /sdcard/apps/<name>.hb (card reader, or companion FILE_WRITE),"
echo "then launch it from the device: apprun <name>  (or menu -> DEV -> Apps)."
