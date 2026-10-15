/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef port_sodium_wrapper_H
#define port_sodium_wrapper_H

/* Pre-include port-specific hash state headers via angle brackets.
* Angle brackets search -I paths directly, so port_include/sodium/
* is found before the original. Once included, their guards are set,
* so when the original sodium.h tries to include them via relative
* double-quote paths, they are silently skipped. */
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_hash_sha512.h>

/* Delegate to the original sodium.h. #include_next skips port_include/
* and finds the next sodium.h in the search path. */
#include_next "sodium.h"

#endif /* port_sodium_wrapper_H */
