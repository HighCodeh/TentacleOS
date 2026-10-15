# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0
from dnslib import (AAAA, PTR, RR, SRV, TXT, A, DNSHeader, DNSQuestion,
                    DNSRecord)


def create_mdns_packet(queries, answers, additional, output_file='mdns_packet.bin'):
    dns_header = DNSHeader(id=0, qr=1, aa=1, ra=0)
    dns_record = DNSRecord(dns_header)
    for qname, qtype in queries:
        dns_record.add_question(DNSQuestion(qname, qtype))
    for name, qtype, value, ttl in answers:
        if qtype == 'A':
            rr = RR(name, rdata=A(value), ttl=ttl)
        elif qtype == 'PTR':
            rr = RR(name, rdata=PTR(value), ttl=ttl)
        elif qtype == 'TXT':
            rr = RR(name, rdata=TXT(value), ttl=ttl)
        elif qtype == 'AAAA':
            rr = RR(name, rdata=AAAA(value), ttl=ttl)
        elif qtype == 'SRV':
            # SRV value should be in format: "priority weight port target"
            parts = value.split()
            priority, weight, port = map(int, parts[:3])
            target = parts[3]
            rr = RR(name, rdata=SRV(priority, weight, port, target), ttl=ttl)
        else:
            print(f'Unsupported type: {qtype}')
            continue
        dns_record.add_answer(rr)
    for name, qtype, value, ttl in additional:
        if qtype == 'A':
            rr = RR(name, rdata=A(value), ttl=ttl)
        elif qtype == 'TXT':
            rr = RR(name, rdata=TXT(value), ttl=ttl)
        elif qtype == 'AAAA':
            rr = RR(name, rdata=AAAA(value), ttl=ttl)
        elif qtype == 'SRV':
            parts = value.split()
            priority, weight, port = map(int, parts[:3])
            target = parts[3]
            rr = RR(name, rdata=SRV(priority, weight, port, target), ttl=ttl)
        else:
            print(f'Unsupported type: {qtype}')
            continue
        dns_record.add_ar(rr)
    mdns_payload = dns_record.pack()

    with open(output_file, 'wb') as f:
        f.write(mdns_payload)

    print(f'mDNS packet saved as {output_file}')


# Test case 1: Basic hostname queries
queries = [
    ('test4.local.', 1),  # A record for main hostname
    ('test.local.', 1),  # A record for main hostname
]
answers = [
]
additional = [
]
create_mdns_packet(queries, answers, additional, 'test_hostname_queries.bin')
print('Test case 1: Hostname queries')

# Test case 2: HTTP service discovery with subtypes
queries = [
    ('_http._tcp.local.', 12),  # PTR query
    ('subtype._sub._http._tcp.local.', 12),  # Subtype query
]
answers = [
    ('_http._tcp.local.', 'PTR', 'inst1._http._tcp.local.', 120),
    ('_http._tcp.local.', 'PTR', 'inst2._http._tcp.local.', 120),
    ('subtype._sub._http._tcp.local.', 'PTR', 'inst1._http._tcp.local.', 120),
]
additional = [
    ('inst1._http._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
    ('inst1._http._tcp.local.', 'TXT', 'board=esp32\0tcp_check=no\0ssh_upload=no\0auth_upload=no', 120),
    ('inst2._http._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
    ('inst2._http._tcp.local.', 'TXT', 'board=esp32\0tcp_check=no\0ssh_upload=no\0auth_upload=no', 120),
]
create_mdns_packet(queries, answers, additional, 'test_http_services.bin')
print('Test case 2: HTTP service discovery with subtypes')

# Test case 3: Scanner service discovery
queries = [
    ('_scanner._tcp.local.', 12),  # PTR query
]
answers = [
    ('_scanner._tcp.local.', 'PTR', 'inst4._scanner._tcp.local.', 120),
    ('_scanner._tcp.local.', 'PTR', 'inst5._scanner._tcp.local.', 120),
]
additional = [
    ('inst4._scanner._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
    ('inst5._scanner._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
]
create_mdns_packet(queries, answers, additional, 'test_scanner_services.bin')
print('Test case 3: Scanner service discovery')

# Test case 4: UDP service (sleep protocol)
queries = [
    ('_sleep._udp.local.', 12),  # PTR query
]
answers = [
    ('_sleep._udp.local.', 'PTR', 'inst7._sleep._udp.local.', 120),
]
additional = [
    ('inst7._sleep._udp.local.', 'SRV', '0 0 80 test.local.', 120),
]
create_mdns_packet(queries, answers, additional, 'test_udp_service.bin')
print('Test case 4: UDP service discovery')

# Test case 5: Async queries from main.c
queries = [
    ('host_name.local.', 1),    # A query
    ('host_name2.local.', 28),  # AAAA query
    ('minifritz._http._tcp.local.', 12),  # PTR query
]
answers = [
    ('host_name.local.', 'A', '192.168.1.10', 120),
    ('host_name2.local.', 'AAAA', 'fe80::1234', 120),
    ('minifritz._http._tcp.local.', 'PTR', 'minifritz._http._tcp.local.', 120),
]
additional = [
    ('minifritz._http._tcp.local.', 'SRV', '0 0 80 minifritz.local.', 120),
    ('minifritz.local.', 'A', '192.168.1.20', 120),
]
create_mdns_packet(queries, answers, additional, 'test_async_queries.bin')
print('Test case 5: Async queries')

# Test case 6: Multiple services on same instance
queries = [
    ('_http._tcp.local.', 12),
    ('_scanner._tcp.local.', 12),
]
answers = [
    ('_http._tcp.local.', 'PTR', 'inst1._http._tcp.local.', 120),
    ('_http._tcp.local.', 'PTR', 'inst2._http._tcp.local.', 120),
    ('_http._tcp.local.', 'PTR', 'inst3._http._tcp.local.', 120),
    ('_scanner._tcp.local.', 'PTR', 'inst4._scanner._tcp.local.', 120),
]
additional = [
    ('inst1._http._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
    ('inst2._http._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
    ('test.local.', 'A', '192.168.1.100', 120),
]
create_mdns_packet(queries, answers, additional, 'test_multiple_services.bin')
print('Test case 6: Multiple services')

# Test case 7: Subtype queries for different hosts
queries = [
    ('subtype._sub._http._tcp.local.', 12),
    ('subtype3._sub._http._tcp.local.', 12),
]
answers = [
    ('subtype._sub._http._tcp.local.', 'PTR', 'inst1._http._tcp.local.', 120),
    ('subtype3._sub._http._tcp.local.', 'PTR', 'inst2._http._tcp.local.', 120),
]
additional = [
    ('inst1._http._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
    ('inst2._http._tcp.local.', 'SRV', '0 0 80 test.local.', 120),
    ('test.local.', 'A', '192.168.1.100', 120),
]
create_mdns_packet(queries, answers, additional, 'test_subtype_queries.bin')
print('Test case 7: Subtype queries')
