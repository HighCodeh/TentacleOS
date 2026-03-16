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

# Source ESP-IDF environment
if [ -f "$HOME/esp/v5.5.1/esp-idf/export.sh" ]; then
    source "$HOME/esp/v5.5.1/esp-idf/export.sh"
else
    echo -e "${RED}Error: ESP-IDF export.sh not found at ~/esp/v5.5.1/esp-idf/export.sh${NC}"
    exit 1
fi

echo -e "${BLUE}>>> Starting TentacleOS Build${NC}"

# 1. Build ESP32-C5 (Radio Co-processor)
echo -e "${BLUE}>>> Building ESP32-C5 firmware...${NC}"
cd "$C5_DIR" || exit
idf.py reconfigure build

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to build C5 firmware.${NC}"
    exit 1
fi

# 2. Build ESP32-P4 (Main OS)
echo -e "${BLUE}>>> Building ESP32-P4 firmware (Embedding C5 binary)...${NC}"
cd "$P4_DIR" || exit
idf.py reconfigure build

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to build P4 firmware.${NC}"
    exit 1
fi

echo -e "${GREEN}>>> TentacleOS build complete!${NC}"
