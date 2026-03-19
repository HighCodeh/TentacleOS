/**
 * @file nfc_common.h
 * @brief Common utilities for the NFC stack.
 */
#ifndef NFC_COMMON_H
#define NFC_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

/** Log a byte array as hex. */
void nfc_log_hex(const char* label, const uint8_t* data, size_t len);

#endif
