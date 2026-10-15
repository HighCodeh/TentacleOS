/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "unity.h"
#include "create_test_packet.h"
#include "unity_main.h"
#include "mock_mdns_pcb.h"
#include "mock_mdns_send.h"
#include "create_test_packet.h"

static void test_mdns_hostname_queries(void)
{
    // Define the queries for test4.local and test.local
    mdns_test_query_t queries[] = {
        { "test4.local", 1, 1 },  // A record for test4.local
        { "test.local", 1, 1 }    // A record for test.local
    };

    // Create and send the packet
    size_t packet_len;
    uint8_t* packet = create_mdns_test_packet(
                          queries, 2,    // Queries
                          NULL, 0,       // No answers
                          NULL, 0,       // No additional records
                          &packet_len
                      );

    send_test_packet_multiple(packet, packet_len);
}

// Example of a more complex test with answers and additional records
static void test_mdns_with_answers(void)
{
    // Define a query for _http._tcp.local PTR record
    mdns_test_query_t queries[] = {
        { "_http._tcp.local", 12, 1 }  // PTR record
    };

    // Example data for a PTR record (simplified)
    uint8_t ptr_data[200];
    size_t ptr_data_len = encode_dns_name(ptr_data, "test._http._tcp.local");

    // Define an answer for the PTR record
    mdns_test_answer_t answers[] = {
        { "_http._tcp.local", 12, 1, 120, ptr_data_len, ptr_data }
    };

    // Create and send the packet
    size_t packet_len;
    uint8_t* packet = create_mdns_test_packet(
                          queries, 1,    // Single query
                          answers, 1,    // Single answer
                          NULL, 0,       // No additional records
                          &packet_len
                      );

    send_test_packet_multiple(packet, packet_len);

}

static void mdns_priv_create_answer_from_parsed_packet_Callback(mdns_parsed_packet_t* parsed_packet, int cmock_num_calls)
{
    printf("callback\n");
}

void setup_cmock(void)
{
    mdns_priv_probe_all_pcbs_CMockIgnore();
    mdns_priv_pcb_announce_CMockIgnore();
    mdns_priv_pcb_send_bye_service_CMockIgnore();
    mdns_priv_pcb_check_probing_services_CMockIgnore();
    mdns_priv_pcb_is_after_probing_IgnoreAndReturn(true);

    mdns_priv_clear_tx_queue_CMockIgnore();
    mdns_priv_remove_scheduled_service_packets_CMockIgnore();
    mdns_priv_create_answer_from_parsed_packet_Stub(mdns_priv_create_answer_from_parsed_packet_Callback);
}

void run_unity_tests(void)
{
    UNITY_BEGIN();

    // Run hostname queries test
    RUN_TEST(test_mdns_hostname_queries);

    // Run test with answers
    RUN_TEST(test_mdns_with_answers);

    UNITY_END();
}
