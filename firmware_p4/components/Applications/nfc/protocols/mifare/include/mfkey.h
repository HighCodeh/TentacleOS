/**
 * @file mfkey.h
 * @brief MFKey key recovery attacks for MIFARE Classic.
 */
#ifndef MFKEY_H
#define MFKEY_H

#include <stdint.h>
#include <stdbool.h>

/** Recover key from two nonces (nested attack). */
bool mfkey32(uint32_t uid, uint32_t nt0, uint32_t nr0, uint32_t ar0,
             uint32_t nt1, uint32_t nr1, uint32_t ar1, uint64_t* key);

#endif
