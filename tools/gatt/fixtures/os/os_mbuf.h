#pragma once
#include <stdint.h>
struct os_mbuf {uint16_t length;uint8_t data[100];};
#define OS_MBUF_PKTLEN(p) ((p)->length)
int os_mbuf_copydata(struct os_mbuf *,int,int,void *);
