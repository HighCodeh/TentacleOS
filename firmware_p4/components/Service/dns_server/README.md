# DNS Server Service Component

This component implements a lightweight DNS server optimized for "Evil Twin" and Captive Portal applications. It intercepts all DNS queries and responds authoritatively with the device's own IP address, effectively redirecting all traffic to the local web server.

## Overview

- **Location:** `components/Service/dns_server/`
- **Main Header:** `include/dns_server.h`
- **Socket Type:** UDP Port 53
- **Response Strategy:** Authoritative (AA=1), Recursive (RA=0), No Error.
- **Dependencies:** `lwip/sockets`, `esp_netif`

## Key Features

- **Dynamic IP Resolution:** Automatically detects the current Access Point IP address using `esp_netif_get_ip_info`, ensuring correct redirection even if the network configuration changes.
- **Robust Parsing:** Implements a safe DNS name parser (`parse_dns_name`) to validate queries and prevent buffer overflows.
- **Evil Twin Optimization:** Uses specific DNS flags (`0x8500`) to mark responses as "Authoritative". This forces client devices (especially modern Android/iOS) to accept the redirection faster, improving Captive Portal detection.
- **IPv4 Focus:** Optimized for stability and simplicity, handling standard A-record queries.
- **Task Management:** Runs in a dedicated FreeRTOS task with an increased stack size (4096 bytes) to handle high loads and logging without overflow.

## API Reference

### `start_dns_server`
```c
void start_dns_server(void);
```
Starts the DNS server task.
- Creates a UDP socket bound to port 53.
- Listens for incoming queries.
- Spawns the `dns_server` task with 4KB stack.

### `stop_dns_server`
```c
void stop_dns_server(void);
```
Stops the DNS server and frees resources.
- Deletes the FreeRTOS task.
- Closes the UDP socket (handled within the task loop upon deletion).

## Internal Implementation Details

### Packet Handling
1.  **Validation:** Incoming packets are checked for minimum size (header length) and valid query flags.
2.  **Parsing:** The domain name is extracted using `parse_dns_name` for logging and validation purposes.
3.  **Response Construction:**
    -   Copies the transaction ID from the request.
    -   Sets Flags to `0x8500` (Response + Authoritative).
    -   Appends the original Question section.
    -   Appends an Answer section pointing to the AP's IP address (TTL 60s).

### Configuration
- **Stack Size:** 4096 bytes (Safe for logging and network operations).
- **Socket Timeout:** 1 second (allows graceful shutdown checks).
