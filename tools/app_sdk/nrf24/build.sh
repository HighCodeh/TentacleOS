#!/usr/bin/env bash
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="$HERE/.."
ROOT="$SDK/../.."
riscv32-esp-elf-gcc -march=rv32imafc_zicsr_zifencei -mabi=ilp32f -Os -ffreestanding -fno-common -mno-relax -ffunction-sections -fdata-sections -Wall -Wextra -Werror -I"$ROOT/firmware_p4/components/Service/app_runtime/include" -c "$HERE/app.c" -o "$HERE/app.elf"
if [ -n "$(riscv32-esp-elf-nm -u "$HERE/app.elf")" ]; then
  echo "Unexpected unresolved app imports" >&2
  exit 1
fi
"${SYSTEM_PYTHON:-/usr/bin/python3}" "$SDK/hb_pack.py" "$HERE/app.elf" "$HERE/NRF24.hb" --name NRF24 --caps 0x120 --sign "${TOS_APP_SIGNING_KEY:-$SDK/keys/dev_ed25519.key}" --abi-major 1 --abi-minor 24
sed -n '2,15s/^# *//p' "$0" > "$HERE/NRF24.hb.license"
cp "$HERE/NRF24.hb.license" "$HERE/app.elf.license"
