#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int socklen_t;

#define AF_INET  2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IP  0
#define SOL_SOCKET  0x01
#define SO_RCVTIMEO 0x02
#define INADDR_ANY  0

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    union {
        uint32_t s_addr;
        struct { uint8_t b1, b2, b3, b4; };
    } sin_addr;
    uint8_t sin_zero[8];
};

typedef struct {
    uint32_t s_addr;
} in_addr_t;

int lwip_socket(int domain, int type, int protocol);
int lwip_bind(int s, const struct sockaddr_in *addr, socklen_t len);
int lwip_listen(int s, int backlog);
int lwip_accept(int s, struct sockaddr_in *addr, socklen_t *len);
int lwip_send(int s, const void *data, size_t len, int flags);
int lwip_recv(int s, void *data, size_t len, int flags);
int lwip_close(int s);
int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);

#define htons(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define htonl(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) >> 8) & 0xFF00) | (((x) >> 24) & 0xFF))

#ifdef __cplusplus
}
#endif
