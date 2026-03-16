#!/bin/bash

PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &> /dev/null && pwd)
P4_DIR="$PROJECT_ROOT/firmware_p4"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

if [ -f "$HOME/esp/v5.5.1/esp-idf/export.sh" ]; then
    source "$HOME/esp/v5.5.1/esp-idf/export.sh"
else
    echo -e "${RED}Error: ESP-IDF export.sh not found at ~/esp/v5.5.1/esp-idf/export.sh${NC}"
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
