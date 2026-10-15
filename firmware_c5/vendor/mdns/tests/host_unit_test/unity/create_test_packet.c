/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "esp_err.h"
#include "unity.h"
#include "create_test_packet.h"

// Encode a domain name in DNS format (length byte + characters)
size_t encode_dns_name(uint8_t* buffer, const char* name)
{
    size_t pos = 0;
    const char* segment_start = name;

    // Process each segment (separated by dots)
    for (const char* p = name; ; p++) {
        if (*p == '.' || *p == 0) {
            // Calculate segment length
            size_t segment_len = p - segment_start;

            // Write length byte
            buffer[pos++] = segment_len;

            // Write segment characters
            for (size_t i = 0; i < segment_len; i++) {
                buffer[pos++] = segment_start[i];
            }

            // If at end of string, exit loop
            if (*p == 0) {
                break;
            }

            // Move to next segment
            segment_start = p + 1;
        }
    }

    // Add null terminator
    buffer[pos++] = 0;

    return pos;
}

// Create an mDNS test packet from arrays of queries and answers
uint8_t* create_mdns_test_packet(
    mdns_test_query_t queries[], size_t query_count,
    mdns_test_answer_t answers[], size_t answer_count,
    mdns_test_answer_t additional[], size_t additional_count,
    size_t* packet_len)
{
    // Allocate buffer for packet (max reasonable size for test)
    uint8_t* packet = malloc(1460);
    size_t pos = 0;

    // DNS Header (12 bytes)
    // Transaction ID
    packet[pos++] = 0x00;
    packet[pos++] = 0x00;

    // Flags (QR=1 for response if there are answers, AA=1)
    uint16_t flags = 0x0000;
    if (answer_count > 0) {
        flags |= 0x8400;  // QR=1, AA=1
    }
    packet[pos++] = (flags >> 8) & 0xFF;
    packet[pos++] = flags & 0xFF;

    // Counts
    packet[pos++] = 0x00;
    packet[pos++] = query_count & 0xFF;  // QDCOUNT
    packet[pos++] = 0x00;
    packet[pos++] = answer_count & 0xFF; // ANCOUNT
    packet[pos++] = 0x00;
    packet[pos++] = 0x00;                // NSCOUNT
    packet[pos++] = 0x00;
    packet[pos++] = additional_count & 0xFF; // ARCOUNT

    // Add queries
    for (size_t i = 0; i < query_count; i++) {
        // Encode name
        pos += encode_dns_name(packet + pos, queries[i].name);

        // Type
        packet[pos++] = (queries[i].type >> 8) & 0xFF;
        packet[pos++] = queries[i].type & 0xFF;

        // Class
        packet[pos++] = (queries[i].class >> 8) & 0xFF;
        packet[pos++] = queries[i].class & 0xFF;
    }

    // Add answers
    for (size_t i = 0; i < answer_count; i++) {
        // Encode name
        pos += encode_dns_name(packet + pos, answers[i].name);

        // Type
        packet[pos++] = (answers[i].type >> 8) & 0xFF;
        packet[pos++] = answers[i].type & 0xFF;

        // Class
        packet[pos++] = (answers[i].class >> 8) & 0xFF;
        packet[pos++] = answers[i].class & 0xFF;

        // TTL (4 bytes)
        packet[pos++] = (answers[i].ttl >> 24) & 0xFF;
        packet[pos++] = (answers[i].ttl >> 16) & 0xFF;
        packet[pos++] = (answers[i].ttl >> 8) & 0xFF;
        packet[pos++] = answers[i].ttl & 0xFF;

        // Data length
        packet[pos++] = (answers[i].data_len >> 8) & 0xFF;
        packet[pos++] = answers[i].data_len & 0xFF;

        // Data
        if (answers[i].data && answers[i].data_len > 0) {
            memcpy(packet + pos, answers[i].data, answers[i].data_len);
            pos += answers[i].data_len;
        }
    }

    // Add additional records
    for (size_t i = 0; i < additional_count; i++) {
        // Encode name
        pos += encode_dns_name(packet + pos, additional[i].name);

        // Type
        packet[pos++] = (additional[i].type >> 8) & 0xFF;
        packet[pos++] = additional[i].type & 0xFF;

        // Class
        packet[pos++] = (additional[i].class >> 8) & 0xFF;
        packet[pos++] = additional[i].class & 0xFF;

        // TTL (4 bytes)
        packet[pos++] = (additional[i].ttl >> 24) & 0xFF;
        packet[pos++] = (additional[i].ttl >> 16) & 0xFF;
        packet[pos++] = (additional[i].ttl >> 8) & 0xFF;
        packet[pos++] = additional[i].ttl & 0xFF;

        // Data length
        packet[pos++] = (additional[i].data_len >> 8) & 0xFF;
        packet[pos++] = additional[i].data_len & 0xFF;

        // Data
        if (additional[i].data && additional[i].data_len > 0) {
            memcpy(packet + pos, additional[i].data, additional[i].data_len);
            pos += additional[i].data_len;
        }
    }

    *packet_len = pos;
    return packet;
}
