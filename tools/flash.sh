#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later

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


PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &> /dev/null && pwd)
if [ -f "$PROJECT_ROOT/integration/import.json" ]; then
    exec bash "$PROJECT_ROOT/tools/highboy/flash.command" "$@"
fi
P4_DIR="$PROJECT_ROOT/firmware_p4"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

if [ -f "$HOME/esp/v6.1/esp-idf/export.sh" ]; then
    source "$HOME/esp/v6.1/esp-idf/export.sh"
else
    echo -e "${RED}Error: ESP-IDF export.sh not found at ~/esp/v6.1/esp-idf/export.sh${NC}"
    exit 1
fi

echo -e "${GREEN}>>> Flashing P4 Master...${NC}"
cd "$P4_DIR" || exit
idf.py flash

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to flash P4 firmware.${NC}"
    exit 1
fi

echo -e "${GREEN}>>> TentacleOS successfully flashed to P4 Master!${NC}"
