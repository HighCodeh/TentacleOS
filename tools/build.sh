#!/bin/bash
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

# Configuration
PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &> /dev/null && pwd)
if [ -f "$PROJECT_ROOT/integration/import.json" ]; then
    exec bash "$PROJECT_ROOT/tools/highboy/build.command" "$@"
fi
C5_DIR="$PROJECT_ROOT/firmware_c5"
P4_DIR="$PROJECT_ROOT/firmware_p4"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

# Source ESP-IDF environment (supports local install and Docker container)
if [ -n "$IDF_PATH" ] && [ -f "$IDF_PATH/export.sh" ]; then
    source "$IDF_PATH/export.sh"
elif [ -f "$HOME/esp/v6.1/esp-idf/export.sh" ]; then
    source "$HOME/esp/v6.1/esp-idf/export.sh"
else
    echo -e "${RED}Error: ESP-IDF not found. Set IDF_PATH or install at ~/esp/v6.1/esp-idf/${NC}"
    exit 1
fi

echo -e "${BLUE}>>> Starting TentacleOS Build${NC}"

echo -e "${BLUE}>>> Building ESP32-C5 firmware...${NC}"
cd "$C5_DIR" || exit
idf.py -DIDF_TARGET=esp32c5 reconfigure build

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to build C5 firmware.${NC}"
    exit 1
fi

echo -e "${BLUE}>>> Building ESP32-P4 firmware (Embedding C5 binary)...${NC}"
cd "$P4_DIR" || exit
idf.py -DIDF_TARGET=esp32p4 reconfigure build

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to build P4 firmware.${NC}"
    exit 1
fi

echo -e "${GREEN}>>> TentacleOS build complete!${NC}"
