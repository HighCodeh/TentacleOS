/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once
#include <stdbool.h>

void send_packet(bool ip4, bool mdns_port, uint8_t*data, size_t len);
void send_test_packet_multiple(uint8_t* packet, size_t packet_len);
