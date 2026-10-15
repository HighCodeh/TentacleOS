// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HOST_LINK_FLY_H
#define HOST_LINK_FLY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint8_t host_fly_handle(uint16_t cmd,
                        const uint8_t *payload,
                        uint16_t plen,
                        uint8_t *out,
                        uint16_t out_cap,
                        uint16_t *out_len);
#ifdef __cplusplus
}
#endif
#endif
