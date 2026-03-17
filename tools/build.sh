#!/bin/bash

# Configuration
PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &> /dev/null && pwd)
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
elif [ -f "$HOME/esp/v5.5.1/esp-idf/export.sh" ]; then
    source "$HOME/esp/v5.5.1/esp-idf/export.sh"
else
    echo -e "${RED}Error: ESP-IDF not found. Set IDF_PATH or install at ~/esp/v5.5.1/esp-idf/${NC}"
    exit 1
fi

echo -e "${BLUE}>>> Starting TentacleOS Build${NC}"

echo -e "${BLUE}>>> Building ESP32-C5 firmware...${NC}"
cd "$C5_DIR" || exit
idf.py reconfigure build

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to build C5 firmware.${NC}"
    exit 1
fi

echo -e "${BLUE}>>> Building ESP32-P4 firmware (Embedding C5 binary)...${NC}"
cd "$P4_DIR" || exit
idf.py reconfigure build

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to build P4 firmware.${NC}"
    exit 1
fi

echo -e "${GREEN}>>> TentacleOS build complete!${NC}"
