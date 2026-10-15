# SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0
import ipaddress
import os
import re
import select
import socket
import struct
import time
from threading import Event, Thread

try:
    import dpkt
    import dpkt.dns
except ImportError:
    pass


def _encode_qname(name):
    """Encode a DNS QNAME (e.g. 'host.local' or '100.1.168.192.in-addr.arpa')."""
    qname = b''
    for label in name.split('.'):
        qname += struct.pack('B', len(label)) + label.encode()
    qname += b'\x00'
    return qname


def _decode_dns_name(data, offset, seen=None):
    """Decode a DNS name from packet, handling compression. Returns (name, new_offset)."""
    if seen is None:
        seen = set()
    labels = []
    while offset < len(data):
        if offset in seen:
            break
        length = data[offset]
        if length == 0:
            return ('.'.join(labels), offset + 1)
        if (length & 0xC0) == 0xC0:
            ptr = struct.unpack('!H', data[offset:offset + 2])[0] & 0x3FFF
            if ptr < len(data) and ptr not in seen:
                seen.add(ptr)
                subname, _ = _decode_dns_name(data, ptr, seen)
                return ('.'.join(labels) + ('.' + subname if labels else subname), offset + 2)
            return ('.'.join(labels), offset + 2)
        seen.add(offset)
        offset += 1
        if offset + length <= len(data):
            labels.append(data[offset:offset + length].decode('utf-8', errors='replace'))
        offset += length
    return ('.'.join(labels), offset)


def run_one_shot_query(name, qtype='A', server='224.0.0.251', port=5353, timeout=5, retries=3):
    """
    Pure-python single-shot DNS query to a multicast address (replaces dig +short).
    For qtype='A': name is hostname (e.g. 'esp32.local'), returns bytes with IP(s).
    For qtype='PTR': name is reverse format (e.g. '100.1.168.192.in-addr.arpa'), returns bytes with hostname(s).
    """
    qtype_val = 1 if qtype == 'A' else 12  # A=1, PTR=12
    tx_id = struct.pack('!H', os.getpid() & 0xFFFF)
    flags = struct.pack('!H', 0x0000)
    counts = struct.pack('!HHHH', 1, 0, 0, 0)
    qname = _encode_qname(name)
    qtype_pack = struct.pack('!H', qtype_val)
    qclass = struct.pack('!H', 1)  # IN
    query = tx_id + flags + counts + qname + qtype_pack + qclass

    print('run_one_shot_query: {} qtype={} -> {}:{}'.format(name, qtype, server, port))

    for attempt in range(1, retries + 1):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.setblocking(False)
            sock.bind(('0.0.0.0', 0))
            sock.sendto(query, (server, port))

            deadline = time.time() + timeout
            while time.time() < deadline:
                remaining = max(0.1, deadline - time.time())
                ready, _, _ = select.select([sock], [], [], min(remaining, 1.0))
                if not ready:
                    continue
                data, addr = sock.recvfrom(4096)

                if len(data) < 12:
                    continue

                ancount = struct.unpack('!H', data[6:8])[0]
                offset = 12
                qdcount = struct.unpack('!H', data[4:6])[0]
                for _ in range(qdcount):
                    while offset < len(data):
                        length = data[offset]
                        if length == 0:
                            offset += 1
                            break
                        if (length & 0xC0) == 0xC0:
                            offset += 2
                            break
                        offset += 1 + length
                    offset += 4

                results = []
                for _ in range(ancount):
                    if offset >= len(data):
                        break
                    if (data[offset] & 0xC0) == 0xC0:
                        offset += 2
                    else:
                        while offset < len(data) and data[offset] != 0:
                            offset += 1 + data[offset]
                        offset += 1

                    if offset + 10 > len(data):
                        break
                    rtype = struct.unpack('!H', data[offset:offset + 2])[0]
                    rdlength = struct.unpack('!H', data[offset + 8:offset + 10])[0]
                    offset += 10

                    if qtype == 'A' and rtype == 1 and rdlength == 4 and offset + 4 <= len(data):
                        ip = socket.inet_ntoa(data[offset:offset + 4])
                        results.append(ip)
                    elif qtype == 'PTR' and rtype == 12 and offset + rdlength <= len(data):
                        ptr_name, _ = _decode_dns_name(data, offset)
                        results.append(ptr_name.rstrip('.'))
                    offset += rdlength

                if results:
                    if qtype == 'A':
                        output = '\n'.join(results) + '\n'
                    else:
                        output = '\n'.join(results) + '\n'
                    print('  resolved: {}'.format(', '.join(results)))
                    return output.encode('utf-8')

            print('  attempt {} timed out after {}s'.format(attempt, timeout))
        except Exception as e:
            print('  attempt {} exception: {}'.format(attempt, e))
        finally:
            sock.close()

    print('  all {} attempts failed'.format(retries))
    return b''


def _ip_to_reverse_name(ip_address):
    """Convert IP address to reverse DNS query name (e.g. 192.168.1.100 -> 100.1.168.192.in-addr.arpa)."""
    if ':' in ip_address:
        addr = ipaddress.ip_address(ip_address)
        rev = addr.reverse_pointer  # e.g. 0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa.
        return rev.rstrip('.')
    parts = ip_address.split('.')
    return '.'.join(reversed(parts)) + '.in-addr.arpa'


def get_dns_query_for_esp(esp_host):
    dns = dpkt.dns.DNS(
        b'\x00\x00\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x01'
    )
    dns.qd[0].name = esp_host + u'.local'
    print('Created query for esp host: {} '.format(dns.__repr__()))
    return dns.pack()


def get_dns_answer_to_mdns(tester_host):
    dns = dpkt.dns.DNS(
        b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
    )
    dns.op = dpkt.dns.DNS_QR | dpkt.dns.DNS_AA
    dns.rcode = dpkt.dns.DNS_RCODE_NOERR
    arr = dpkt.dns.DNS.RR()
    arr.cls = dpkt.dns.DNS_IN
    arr.type = dpkt.dns.DNS_A
    arr.name = tester_host
    arr.ip = socket.inet_aton('127.0.0.1')
    dns.an.append(arr)
    print('Created answer to mdns query: {} '.format(dns.__repr__()))
    return dns.pack()


def get_dns_answer_to_mdns_lwip(tester_host, id):
    dns = dpkt.dns.DNS(
        b'\x5e\x39\x84\x00\x00\x01\x00\x01\x00\x00\x00\x00\x0a\x64\x61\x76\x69\x64'
        b'\x2d\x63\x6f\x6d\x70\x05\x6c\x6f\x63\x61\x6c\x00\x00\x01\x00\x01\xc0\x0c'
        b'\x00\x01\x00\x01\x00\x00\x00\x0a\x00\x04\xc0\xa8\x0a\x6c')
    dns.qd[0].name = tester_host
    dns.an[0].name = tester_host
    dns.an[0].ip = socket.inet_aton('127.0.0.1')
    dns.an[0].rdata = socket.inet_aton('127.0.0.1')
    dns.id = id
    print('Created answer to mdns (lwip) query: {} '.format(dns.__repr__()))
    return dns.pack()


def mdns_server(esp_host, events):
    UDP_IP = '0.0.0.0'
    UDP_PORT = 5353
    MCAST_GRP = '224.0.0.251'
    TESTER_NAME = u'tinytester.local'
    TESTER_NAME_LWIP = u'tinytester-lwip.local'
    QUERY_TIMEOUT = 0.2
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sock.setblocking(False)
    sock.bind((UDP_IP, UDP_PORT))
    mreq = struct.pack('4sl', socket.inet_aton(MCAST_GRP), socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    last_query_timepoint = time.time()
    while not events['stop'].is_set():
        try:
            current_time = time.time()
            if current_time - last_query_timepoint > QUERY_TIMEOUT:
                last_query_timepoint = current_time
                if not events['esp_answered'].is_set():
                    sock.sendto(get_dns_query_for_esp(esp_host),
                                (MCAST_GRP, UDP_PORT))
                if not events['esp_delegated_answered'].is_set():
                    sock.sendto(get_dns_query_for_esp(esp_host + '-delegated'),
                                (MCAST_GRP, UDP_PORT))
            timeout = max(
                0, QUERY_TIMEOUT - (current_time - last_query_timepoint))
            read_socks, _, _ = select.select([sock], [], [], timeout)
            if not read_socks:
                continue
            data, addr = sock.recvfrom(1024)
            dns = dpkt.dns.DNS(data)
            if len(dns.qd) > 0:
                for dns_query in dns.qd:
                    if dns_query.type == dpkt.dns.DNS_A:
                        if dns_query.name == TESTER_NAME:
                            print('Received query: {} '.format(dns.__repr__()))
                            sock.sendto(get_dns_answer_to_mdns(TESTER_NAME),
                                        (MCAST_GRP, UDP_PORT))
                        elif dns_query.name == TESTER_NAME_LWIP:
                            print('Received query: {} '.format(dns.__repr__()))
                            sock.sendto(
                                get_dns_answer_to_mdns_lwip(TESTER_NAME_LWIP, dns.id),
                                addr)
            if len(dns.an) > 0:
                for dns_answer in dns.an:
                    if dns_answer.type == dpkt.dns.DNS_A:
                        print('Received answer from {}'.format(dns_answer.name))
                        if dns_answer.name == esp_host + u'.local':
                            print('Received answer to esp32-mdns query: {}'.format(
                                dns.__repr__()))
                            events['esp_answered'].set()
                        if dns_answer.name == esp_host + u'-delegated.local':
                            print('Received answer to esp32-mdns-delegate query: {}'.format(
                                dns.__repr__()))
                            events['esp_delegated_answered'].set()
        except socket.timeout:
            break
        except dpkt.UnpackError:
            continue


def test_examples_protocol_mdns(dut):
    """
    steps: |
      1. obtain IP address + init mdns example
      2. get the dut host name (and IP address)
      3. check the mdns name is accessible
      4. check DUT output if mdns advertized host is resolved
      5. check if DUT responds to dig
      6. check the DUT is searchable via reverse IP lookup
    """

    specific_host = dut.expect(r'mdns hostname set to: \[(.*?)\]')[1].decode()

    mdns_server_events = {
        'stop': Event(),
        'esp_answered': Event(),
        'esp_delegated_answered': Event()
    }
    mdns_responder = Thread(target=mdns_server,
                            args=(str(specific_host), mdns_server_events))
    ip_addresses = []
    if dut.app.sdkconfig.get('LWIP_IPV4') is True:
        ipv4 = dut.expect(r'IPv4 address: (\d+\.\d+\.\d+\.\d+)[^\d]',
                          timeout=30)[1].decode()
        ip_addresses.append(ipv4)
    if dut.app.sdkconfig.get('LWIP_IPV6') is True:
        ipv6_r = r':'.join((r'[0-9a-fA-F]{4}', ) * 8)
        ipv6 = dut.expect(ipv6_r, timeout=30)[0].decode()
        ip_addresses.append(ipv6)
    print('Connected with IP addresses: {}'.format(','.join(ip_addresses)))
    try:
        # TODO: Add test for example disabling IPV4
        mdns_responder.start()
        if dut.app.sdkconfig.get('LWIP_IPV4') is True:
            # 3. check the mdns name is accessible.
            if not mdns_server_events['esp_answered'].wait(timeout=30):
                raise ValueError(
                    'Test has failed: did not receive mdns answer within timeout')
            if not mdns_server_events['esp_delegated_answered'].wait(timeout=30):
                raise ValueError(
                    'Test has failed: did not receive mdns answer for delegated host within timeout'
                )
            # 4. check DUT output if mdns advertized host is resolved
            dut.expect(
                re.compile(
                    b'mdns-test: Query A: tinytester.local resolved to: 127.0.0.1')
            )
            dut.expect(
                re.compile(
                    b'mdns-test: gethostbyname: tinytester-lwip.local resolved to: 127.0.0.1'
                ))
            dut.expect(
                re.compile(
                    b'mdns-test: getaddrinfo: tinytester-lwip.local resolved to: 127.0.0.1'
                ))
            # 5. check the DUT answers to one-shot mDNS query (replaces dig)
            dig_output = run_one_shot_query(
                '{}.local'.format(specific_host), qtype='A'
            )
            print('Resolving {} using one-shot query succeeded with:\n{}'.format(
                specific_host, dig_output))
            if not ipv4.encode('utf-8') in dig_output:
                raise ValueError(
                    'Test has failed: Incorrectly resolved DUT hostname using dig'
                    "Output should've contained DUT's IP address:{}".format(ipv4))
            # 6. check the DUT reverse lookup
            if dut.app.sdkconfig.get('MDNS_RESPOND_REVERSE_QUERIES') is True:
                for ip_address in ip_addresses:
                    reverse_name = _ip_to_reverse_name(ip_address)
                    dig_output = run_one_shot_query(
                        reverse_name, qtype='PTR'
                    )
                    print('Reverse lookup for {} using one-shot query succeeded with:\n{}'.
                          format(ip_address, dig_output))
                    if specific_host not in dig_output.decode():
                        raise ValueError(
                            'Test has failed: Incorrectly resolved DUT IP address using dig'
                            "Output should've contained DUT's name:{}".format(
                                specific_host))

    finally:
        mdns_server_events['stop'].set()
        mdns_responder.join()
