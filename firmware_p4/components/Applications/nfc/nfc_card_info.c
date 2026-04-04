// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "nfc_card_info.h"
#include <stddef.h>

const char* get_manufacturer_name(uint8_t uid0)
{
    switch (uid0) {
    case 0x01: return "Motorola";
    case 0x02: return "STMicroelectronics";
    case 0x03: return "Hitachi";
    case 0x04: return "NXP Semiconductors";
    case 0x05: return "Infineon Technologies";
    case 0x06: return "Cylink";
    case 0x07: return "Texas Instruments";
    case 0x08: return "Fujitsu";
    case 0x09: return "Matsushita";
    case 0x0A: return "NEC";
    case 0x0B: return "Oki Electric";
    case 0x0C: return "Toshiba";
    case 0x0D: return "Mitsubishi Electric";
    case 0x0E: return "Samsung Electronics";
    case 0x0F: return "Hynix";
    case 0x10: return "LG Semiconductors";
    case 0x11: return "Emosyn-EM Microelectronics";
    case 0x12: return "INSIDE Technology";
    case 0x13: return "ORGA Kartensysteme";
    case 0x14: return "SHARP";
    case 0x15: return "ATMEL";
    case 0x16: return "EM Microelectronic-Marin";
    case 0x17: return "SMARTRAC";
    case 0x18: return "ZMD";
    case 0x19: return "XICOR";
    case 0x1A: return "Sony";
    case 0x1B: return "Malaysia Microelectronic Solutions";
    case 0x1C: return "Emosyn";
    case 0x1D: return "Shanghai Fudan Microelectronics";
    case 0x1E: return "Magellan Technology";
    case 0x1F: return "Melexis";
    case 0x20: return "Renesas Technology";
    case 0x21: return "TAGSYS";
    case 0x22: return "Transcore";
    case 0x23: return "Shanghai Belling";
    case 0x24: return "Masktech";
    case 0x25: return "Innovision R&T";
    case 0x26: return "Hitachi ULSI Systems";
    case 0x27: return "Yubico";
    case 0x28: return "Ricoh";
    case 0x29: return "ASK";
    case 0x2A: return "Unicore Microsystems";
    case 0x2B: return "Dallas/Maxim";
    case 0x2C: return "Impinj";
    case 0x2D: return "RightPlug Alliance";
    case 0x2E: return "Broadcom";
    case 0x2F: return "MStar Semiconductor";
    case 0x50: return "HID Global";
    case 0x88: return "Infineon Technologies (cascade)";
    case 0xE0: return "Unknown (Chinese clone)";
    default:   return NULL;
    }
}

card_type_info_t identify_card(uint8_t sak, const uint8_t atqa[2])
{
    card_type_info_t info = { "Unknown", "Unknown NFC-A Tag", false, false, false };

    if (sak == 0x00) {
        info.name = "NTAG/Ultralight";
        info.full_name = "MIFARE Ultralight / NTAG";
        info.is_mf_ultralight = true;
    }
    else if (sak == 0x08) {
        info.name = "Classic 1K";
        info.is_mf_classic = true;
        if (atqa[0] == 0x44 && atqa[1] == 0x00) {
            info.full_name = "MIFARE Classic 1K (4-byte NUID)";
        } else if (atqa[0] == 0x04 && atqa[1] == 0x04) {
            info.full_name = "MIFARE Plus 2K (SL1)";
        } else if (atqa[0] == 0x44 && atqa[1] == 0x04) {
            info.full_name = "MIFARE Plus 2K (SL1 7b)";
        } else {
            info.full_name = "MIFARE Classic 1K";
        }
    }
    else if (sak == 0x88) {
        info.name = "Classic 1K";
        info.full_name = "MIFARE Classic (SAK 0x88)";
        info.is_mf_classic = true;
    }
    else if (sak == 0x09) {
        info.name = "Classic Mini";
        info.full_name = "MIFARE Classic Mini 0.3K (320 bytes)";
        info.is_mf_classic = true;
    }
    else if (sak == 0x10) {
        info.name = "Plus 2K SL2";
        info.full_name = "MIFARE Plus 2K (SL2)";
        info.is_mf_classic = true;
    }
    else if (sak == 0x11) {
        info.name = "Plus 4K SL2";
        info.full_name = "MIFARE Plus 4K (SL2)";
        info.is_mf_classic = true;
    }
    else if (sak == 0x18) {
        info.name = "Classic 4K";
        info.full_name = "MIFARE Classic 4K";
        info.is_mf_classic = true;
    }
    else if (sak == 0x01) {
        info.name = "Classic 1K TNP";
        info.full_name = "TNP3XXX (Classic 1K protocol)";
        info.is_mf_classic = true;
    }
    else if (sak == 0x28) {
        info.name = "Classic 1K-EV1";
        info.full_name = "MIFARE Classic 1K (emulated / EV1)";
        info.is_mf_classic = true;
    }
    else if (sak == 0x38) {
        info.name = "Classic 4K-EV1";
        info.full_name = "MIFARE Classic 4K (emulated / EV1)";
        info.is_mf_classic = true;
    }
    else if (sak & 0x20) {
        info.name = "ISO-DEP";
        info.is_iso_dep = true;
        if (sak == 0x20) info.full_name = "ISO 14443-4 (DESFire / Plus SL3 / JCOP)";
        else if (sak == 0x60) info.full_name = "ISO 14443-4 (CL3 cascade)";
        else info.full_name = "ISO 14443-4 Compatible";
    }

    return info;
}
