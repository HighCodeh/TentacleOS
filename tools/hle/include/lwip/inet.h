#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t lwip_ntohl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t lwip_ntohs(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t lwip_htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t lwip_htons(uint16_t x) { return __builtin_bswap16(x); }

char *lwip_inet_ntoa(uint32_t addr);
int  lwip_inet_aton(const char *cp, uint32_t *addr);

#ifdef __cplusplus
}
#endif
