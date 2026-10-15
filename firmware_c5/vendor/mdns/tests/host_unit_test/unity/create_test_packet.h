/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once
// Structure for a DNS query
typedef struct {
    const char* name;     // Name to query (e.g., "test.local")
    uint16_t type;        // Query type (e.g., MDNS_TYPE_A)
    uint16_t class;       // Query class (typically 1 for IN)
} mdns_test_query_t;

// Structure for a DNS answer record
typedef struct {
    const char* name;     // Name this record refers to
    uint16_t type;        // Record type
    uint16_t class;       // Record class
    uint32_t ttl;         // Time to live
    uint16_t data_len;    // Length of data
    uint8_t* data;        // Record data
} mdns_test_answer_t;


uint8_t* create_mdns_test_packet(
    mdns_test_query_t queries[], size_t query_count,
    mdns_test_answer_t answers[], size_t answer_count,
    mdns_test_answer_t additional[], size_t additional_count,
    size_t* packet_len);

size_t encode_dns_name(uint8_t* buffer, const char* name);
