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
/**
 * @file highboy_nfc_error.h
 * @brief High Boy NFC Error codes.
 */
#ifndef HIGHBOY_NFC_ERROR_H
#define HIGHBOY_NFC_ERROR_H

typedef enum {
    HB_NFC_OK = 0,
    
    HB_NFC_ERR_SPI_INIT     = 0x01,
    HB_NFC_ERR_SPI_XFER     = 0x02,
    HB_NFC_ERR_GPIO          = 0x03,
    HB_NFC_ERR_TIMEOUT       = 0x04,
    
    HB_NFC_ERR_CHIP_ID      = 0x20,
    HB_NFC_ERR_FIFO_OVR     = 0x21,
    HB_NFC_ERR_FIELD         = 0x22,
    
    HB_NFC_ERR_NO_CARD      = 0x40,
    HB_NFC_ERR_NOT_FOUND    = 0x47,
    HB_NFC_ERR_CRC           = 0x41,
    HB_NFC_ERR_COLLISION     = 0x42,
    HB_NFC_ERR_NACK          = 0x43,
    HB_NFC_ERR_AUTH          = 0x44,
    HB_NFC_ERR_PROTOCOL      = 0x45,
    HB_NFC_ERR_TX_TIMEOUT   = 0x46,
    
    HB_NFC_ERR_PARAM         = 0xFE,
    HB_NFC_ERR_INTERNAL      = 0xFF,
} hb_nfc_err_t;

const char* hb_nfc_err_str(hb_nfc_err_t err);

#endif
