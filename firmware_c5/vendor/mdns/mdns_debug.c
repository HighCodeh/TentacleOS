/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "mdns_private.h"
#include "mdns_utils.h"

#ifdef CONFIG_MDNS_DEBUG_USE_ESP_LOG

#include <stdarg.h>
#include "esp_log.h"

#define MDNS_DBG_MAX_LINE CONFIG_MDNS_DEBUG_BUFFER_SIZE

static char s_mdns_dbg_buf[MDNS_DBG_MAX_LINE];
static size_t s_mdns_dbg_pos = 0;

static void mdns_dbg_puts(const char *str)
{
    ESP_LOGI("mdns", "%s", str);
}

static inline void mdns_dbg_flush(void)
{
    if (s_mdns_dbg_pos > 0) {
        s_mdns_dbg_buf[s_mdns_dbg_pos] = '\0';
        mdns_dbg_puts(s_mdns_dbg_buf);
        s_mdns_dbg_pos = 0;
    }
}

static void mdns_dbg_vprintf(const char *fmt, va_list args)
{
    // Try to format directly into the buffer
    int len = vsnprintf(s_mdns_dbg_buf + s_mdns_dbg_pos,
                        MDNS_DBG_MAX_LINE - s_mdns_dbg_pos,
                        fmt, args);

    if (len < 0) {
        return; // Error in formatting
    }

    // Check if the entire formatted string fit in the buffer
    if (len < (MDNS_DBG_MAX_LINE - s_mdns_dbg_pos)) {
        // If it fit, just update the position
        s_mdns_dbg_pos += len;
    } else {
        // The formatted string was truncated because it didn't fit
        // First, flush what we have (the partial string)
        mdns_dbg_flush();

        // Create a new va_list copy and try again with the full buffer
        va_list args_copy;
        va_copy(args_copy, args);

        // Format again with the entire buffer available
        len = vsnprintf(s_mdns_dbg_buf, MDNS_DBG_MAX_LINE - 1, fmt, args_copy);
        va_end(args_copy);

        if (len < 0) {
            return; // Error
        }

        // Check if content will be lost (true truncation)
        if (len >= MDNS_DBG_MAX_LINE - 1) {
            // This is when actual content will be lost - log a warning
            ESP_LOGW("mdns", "Message truncated: length (%d) exceeds buffer size (%d). Consider increasing CONFIG_MDNS_DEBUG_BUFFER_SIZE.",
                     len, MDNS_DBG_MAX_LINE - 1);

            // Display what we could fit, then flush and return
            s_mdns_dbg_pos = MDNS_DBG_MAX_LINE - 1;
            mdns_dbg_flush();
            return;
        }

        // If we get here, the whole message fit this time
        s_mdns_dbg_pos = len;
    }

    // If buffer is nearly full after this operation, flush it
    if (s_mdns_dbg_pos >= MDNS_DBG_MAX_LINE - 1) {
        mdns_dbg_flush();
    }
}

static void mdns_dbg_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mdns_dbg_vprintf(fmt, ap);
    va_end(ap);
}

#define dbg_printf(...) mdns_dbg_printf(__VA_ARGS__)
#else
#define dbg_printf(...) printf(__VA_ARGS__)
#define mdns_dbg_flush()
#endif

void static dbg_packet(const uint8_t *data, size_t len)
{
    static mdns_name_t n;
    mdns_header_t header;
    const uint8_t *content = data + MDNS_HEAD_LEN;
    uint64_t t = xTaskGetTickCount() * portTICK_PERIOD_MS;
    mdns_name_t *name = &n;
    memset(name, 0, sizeof(mdns_name_t));

    dbg_printf("Packet[%" PRIu64 "]: ", t);

    header.id = mdns_utils_read_u16(data, MDNS_HEAD_ID_OFFSET);
    header.flags = mdns_utils_read_u16(data, MDNS_HEAD_FLAGS_OFFSET);
    header.questions = mdns_utils_read_u16(data, MDNS_HEAD_QUESTIONS_OFFSET);
    header.answers = mdns_utils_read_u16(data, MDNS_HEAD_ANSWERS_OFFSET);
    header.servers = mdns_utils_read_u16(data, MDNS_HEAD_SERVERS_OFFSET);
    header.additional = mdns_utils_read_u16(data, MDNS_HEAD_ADDITIONAL_OFFSET);

    dbg_printf("%s",
               (header.flags == MDNS_FLAGS_QR_AUTHORITATIVE) ? "AUTHORITATIVE\n" :
               (header.flags == MDNS_FLAGS_DISTRIBUTED) ? "DISTRIBUTED\n" :
               (header.flags == 0) ? "\n" : " "
              );
    if (header.flags && header.flags != MDNS_FLAGS_QR_AUTHORITATIVE) {
        dbg_printf("0x%04X\n", header.flags);
    }

    if (header.questions) {
        uint8_t qs = header.questions;

        while (qs--) {
            content = mdns_utils_parse_fqdn(data, content, name, len);
            if (!content || content + MDNS_CLASS_OFFSET + 1 >= data + len) {
                header.answers = 0;
                header.additional = 0;
                header.servers = 0;
                dbg_printf("ERROR: parse header questions\n");
                break;
            }

            uint16_t type = mdns_utils_read_u16(content, MDNS_TYPE_OFFSET);
            uint16_t mdns_class = mdns_utils_read_u16(content, MDNS_CLASS_OFFSET);
            bool unicast = !!(mdns_class & 0x8000);
            mdns_class &= 0x7FFF;
            content = content + 4;

            dbg_printf("    Q: ");
            if (unicast) {
                dbg_printf("*U* ");
            }
            if (type == MDNS_TYPE_PTR) {
                dbg_printf("%s.%s%s.%s.%s. PTR ", name->host, name->sub ? "_sub." : "", name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_SRV) {
                dbg_printf("%s.%s%s.%s.%s. SRV ", name->host, name->sub ? "_sub." : "", name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_TXT) {
                dbg_printf("%s.%s%s.%s.%s. TXT ", name->host, name->sub ? "_sub." : "", name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_A) {
                dbg_printf("%s.%s. A ", name->host, name->domain);
            } else if (type == MDNS_TYPE_AAAA) {
                dbg_printf("%s.%s. AAAA ", name->host, name->domain);
            } else if (type == MDNS_TYPE_NSEC) {
                dbg_printf("%s.%s%s.%s.%s. NSEC ", name->host, name->sub ? "_sub." : "", name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_ANY) {
                dbg_printf("%s.%s%s.%s.%s. ANY ", name->host, name->sub ? "_sub." : "", name->service, name->proto, name->domain);
            } else {
                dbg_printf("%s.%s%s.%s.%s. %04X ", name->host, name->sub ? "_sub." : "", name->service, name->proto, name->domain, type);
            }

            if (mdns_class == 0x0001) {
                dbg_printf("IN");
            } else {
                dbg_printf("%04X", mdns_class);
            }
            dbg_printf("\n");
        }
    }

    if (header.answers || header.servers || header.additional) {
        uint16_t recordIndex = 0;

        while (content < (data + len)) {

            content = mdns_utils_parse_fqdn(data, content, name, len);
            if (!content) {
                dbg_printf("ERROR: parse mdns records\n");
                break;
            }

            if (content + MDNS_LEN_OFFSET + 1 >= data + len) {
                // malformed packet, RR header (TYPE/CLASS/TTL/LEN) would read past the buffer
                dbg_printf("ERROR: truncated RR header\n");
                break;
            }
            uint16_t type = mdns_utils_read_u16(content, MDNS_TYPE_OFFSET);
            uint16_t mdns_class = mdns_utils_read_u16(content, MDNS_CLASS_OFFSET);
            uint32_t ttl = mdns_utils_read_u32(content, MDNS_TTL_OFFSET);
            uint16_t data_len = mdns_utils_read_u16(content, MDNS_LEN_OFFSET);
            const uint8_t *data_ptr = content + MDNS_DATA_OFFSET;
            bool flush = !!(mdns_class & 0x8000);
            mdns_class &= 0x7FFF;

            content = data_ptr + data_len;
            if (content > (data + len)) {
                dbg_printf("ERROR: content length overflow\n");
                break;
            }

            mdns_parsed_record_type_t record_type = MDNS_ANSWER;

            if (recordIndex >= (header.answers + header.servers)) {
                record_type = MDNS_EXTRA;
            } else if (recordIndex >= (header.answers)) {
                record_type = MDNS_NS;
            }
            recordIndex++;

            if (record_type == MDNS_EXTRA) {
                dbg_printf("    X");
            } else if (record_type == MDNS_NS) {
                dbg_printf("    S");
            } else {
                dbg_printf("    A");
            }

            if (type == MDNS_TYPE_PTR) {
                dbg_printf(": %s%s%s.%s.%s. PTR ", name->host, name->host[0] ? "." : "", name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_SRV) {
                dbg_printf(": %s.%s.%s.%s. SRV ", name->host, name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_TXT) {
                dbg_printf(": %s.%s.%s.%s. TXT ", name->host, name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_A) {
                dbg_printf(": %s.%s. A ", name->host, name->domain);
            } else if (type == MDNS_TYPE_AAAA) {
                dbg_printf(": %s.%s. AAAA ", name->host, name->domain);
            } else if (type == MDNS_TYPE_NSEC) {
                dbg_printf(": %s.%s.%s.%s. NSEC ", name->host, name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_ANY) {
                dbg_printf(": %s.%s.%s.%s. ANY ", name->host, name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_OPT) {
                dbg_printf(": . OPT ");
            } else {
                dbg_printf(": %s.%s.%s.%s. %04X ", name->host, name->service, name->proto, name->domain, type);
            }

            if (mdns_class == 0x0001) {
                dbg_printf("IN ");
            } else {
                dbg_printf("%04X ", mdns_class);
            }
            if (flush) {
                dbg_printf("FLUSH ");
            }
            dbg_printf("%" PRIu32, ttl);
            dbg_printf("[%u] ", data_len);
            if (type == MDNS_TYPE_PTR) {
                if (!mdns_utils_parse_fqdn(data, data_ptr, name, len)) {
                    dbg_printf("ERROR: parse PTR\n");
                    continue;
                }
                dbg_printf("%s.%s.%s.%s.\n", name->host, name->service, name->proto, name->domain);
            } else if (type == MDNS_TYPE_SRV) {
                if (!mdns_utils_parse_fqdn(data, data_ptr + MDNS_SRV_FQDN_OFFSET, name, len)) {
                    dbg_printf("ERROR: parse SRV\n");
                    continue;
                }
                uint16_t priority = mdns_utils_read_u16(data_ptr, MDNS_SRV_PRIORITY_OFFSET);
                uint16_t weight = mdns_utils_read_u16(data_ptr, MDNS_SRV_WEIGHT_OFFSET);
                uint16_t port = mdns_utils_read_u16(data_ptr, MDNS_SRV_PORT_OFFSET);
                dbg_printf("%u %u %u %s.%s.\n", priority, weight, port, name->host, name->domain);
            } else if (type == MDNS_TYPE_TXT) {
                uint16_t i = 0, y;
                while (i < data_len) {
                    uint8_t partLen = data_ptr[i++];
                    if ((i + partLen) > data_len) {
                        dbg_printf("ERROR: parse TXT\n");
                        break;
                    }
                    char txt[partLen + 1];
                    for (y = 0; y < partLen; y++) {
                        char d = data_ptr[i++];
                        txt[y] = d;
                    }
                    txt[partLen] = 0;
                    dbg_printf("%s", txt);
                    if (i < data_len) {
                        dbg_printf("; ");
                    }
                }
                dbg_printf("\n");
            } else if (type == MDNS_TYPE_AAAA) {
                if (data_len < sizeof(esp_ip6_addr_t)) {
                    dbg_printf("ERROR: truncated AAAA\n");
                    continue;
                }
                esp_ip6_addr_t ip6;
                memcpy(&ip6, data_ptr, sizeof(esp_ip6_addr_t));
                dbg_printf(IPV6STR "\n", IPV62STR(ip6));
            } else if (type == MDNS_TYPE_A) {
                if (data_len < sizeof(esp_ip4_addr_t)) {
                    dbg_printf("ERROR: truncated A\n");
                    continue;
                }
                esp_ip4_addr_t ip;
                memcpy(&ip, data_ptr, sizeof(esp_ip4_addr_t));
                dbg_printf(IPSTR "\n", IP2STR(&ip));
            } else if (type == MDNS_TYPE_NSEC) {
                const uint8_t *old_ptr = data_ptr;
                const uint8_t *new_ptr = mdns_utils_parse_fqdn(data, data_ptr, name, len);
                if (new_ptr) {
                    dbg_printf("%s.%s.%s.%s. ", name->host, name->service, name->proto, name->domain);
                    size_t diff = new_ptr - old_ptr;
                    if (diff > data_len) {
                        // FQDN parsed past the record boundary; avoid underflowing data_len
                        dbg_printf("ERROR: parse NSEC\n");
                        continue;
                    }
                    data_len -= diff;
                    data_ptr = new_ptr;
                }
                size_t i;
                for (i = 0; i < data_len; i++) {
                    dbg_printf(" %02x", data_ptr[i]);
                }
                dbg_printf("\n");
            } else if (type == MDNS_TYPE_OPT) {
                uint16_t opCode = mdns_utils_read_u16(data_ptr, 0);
                uint16_t opLen = mdns_utils_read_u16(data_ptr, 2);
                dbg_printf(" Code: %04x Data[%u]:", opCode, opLen);
                size_t i;
                for (i = 4; i < data_len; i++) {
                    dbg_printf(" %02x", data_ptr[i]);
                }
                dbg_printf("\n");
            } else {
                size_t i;
                for (i = 0; i < data_len; i++) {
                    dbg_printf(" %02x", data_ptr[i]);
                }
                dbg_printf("\n");
            }
        }
    }
    mdns_dbg_flush();
}

void mdns_debug_tx_packet(mdns_tx_packet_t *p, uint8_t packet[MDNS_MAX_PACKET_SIZE], uint16_t index)
{
    dbg_printf("\nTX[%lu][%lu]: ", (unsigned long)p->tcpip_if, (unsigned long)p->ip_protocol);
#ifdef CONFIG_LWIP_IPV4
    if (p->dst.type == ESP_IPADDR_TYPE_V4) {
        dbg_printf("To: " IPSTR ":%u, ", IP2STR(&p->dst.u_addr.ip4), p->port);
    }
#endif
#ifdef CONFIG_LWIP_IPV6
    if (p->dst.type == ESP_IPADDR_TYPE_V6) {
        dbg_printf("To: " IPV6STR ":%u, ", IPV62STR(p->dst.u_addr.ip6), p->port);
    }
#endif
    dbg_packet(packet, index);
    mdns_dbg_flush();
}

void mdns_debug_rx_packet(mdns_rx_packet_t *packet, const uint8_t* data, uint16_t len)
{
    dbg_printf("\nRX[%lu][%lu]: ", (unsigned long)packet->tcpip_if, (unsigned long)packet->ip_protocol);
#ifdef CONFIG_LWIP_IPV4
    if (packet->src.type == ESP_IPADDR_TYPE_V4) {
        dbg_printf("From: " IPSTR ":%u, To: " IPSTR ", ", IP2STR(&packet->src.u_addr.ip4), packet->src_port, IP2STR(&packet->dest.u_addr.ip4));
    }
#endif
#ifdef CONFIG_LWIP_IPV6
    if (packet->src.type == ESP_IPADDR_TYPE_V6) {
        dbg_printf("From: " IPV6STR ":%u, To: " IPV6STR ", ", IPV62STR(packet->src.u_addr.ip6), packet->src_port, IPV62STR(packet->dest.u_addr.ip6));
    }
#endif
    dbg_packet(data, len);
    mdns_dbg_flush();
}

static void dbg_printf_result(mdns_result_t *r_t)
{
    mdns_ip_addr_t *r_a = NULL;
    int addr_count = 0;
    dbg_printf("result esp_netif: %p\n", r_t->esp_netif);
    dbg_printf("result ip_protocol: %d\n", r_t->ip_protocol);
    dbg_printf("result hostname: %s\n", mdns_utils_str_null_or_empty(r_t->hostname) ? "NULL" : r_t->hostname);
    dbg_printf("result instance_name: %s\n", mdns_utils_str_null_or_empty(r_t->instance_name) ? "NULL" : r_t->instance_name);
    dbg_printf("result service_type: %s\n", mdns_utils_str_null_or_empty(r_t->service_type) ? "NULL" : r_t->service_type);
    dbg_printf("result proto: %s\n", mdns_utils_str_null_or_empty(r_t->proto) ? "NULL" : r_t->proto);
    dbg_printf("result port: %d\n", r_t->port);
    dbg_printf("result ttl: %" PRIu32 "\n", r_t->ttl);
    for (int i = 0; i < r_t->txt_count; i++) {
        dbg_printf("result txt item%d, key: %s, value: %s\n", i, r_t->txt[i].key, r_t->txt[i].value);
    }
    r_a = r_t->addr;
    while (r_a) {
#ifdef CONFIG_LWIP_IPV4
        if (r_a->addr.type == ESP_IPADDR_TYPE_V4) {
            dbg_printf("Addr%d: " IPSTR "\n", addr_count++, IP2STR(&r_a->addr.u_addr.ip4));
        }
#endif
#ifdef CONFIG_LWIP_IPV6
        if (r_a->addr.type == ESP_IPADDR_TYPE_V6) {
            dbg_printf("Addr%d: " IPV6STR "\n", addr_count++, IPV62STR(r_a->addr.u_addr.ip6));
        }
#endif
        r_a = r_a->next;
    }
    mdns_dbg_flush();
}

void mdns_debug_printf_browse_result(mdns_result_t *r_t, mdns_browse_t *b_t)
{
    dbg_printf("----------------sync browse %s.%s result---------------\n", b_t->service, b_t->proto);
    dbg_printf("browse pointer: %p\n", b_t);
    dbg_printf_result(r_t);
    mdns_dbg_flush();
}

void mdns_debug_printf_browse_result_all(mdns_result_t *r_t)
{
    int count = 0;
    while (r_t) {
        dbg_printf("----------------result %d---------------\n", count++);
        dbg_printf_result(r_t);
        r_t = r_t->next;
    }
    mdns_dbg_flush();
}
