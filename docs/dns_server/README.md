# DNS Server Service Component

This component implements a lightweight DNS server optimized for "Evil Twin" and Captive Portal applications. It intercepts DNS queries and, like a real captive portal, hijacks them to the device's own IP address so all traffic funnels to the local web server. The single exception is the OS HTTPS connectivity-probe host `www.google.com`, which is answered with `NXDOMAIN` on purpose (see [Captive-portal detection strategy](#captive-portal-detection-strategy)).

## Overview

- **Location:** `components/Service/dns_server/`
- **Main Header:** `include/dns_server.h`
- **Socket Type:** UDP Port 53
- **Response Strategy:** Authoritative (AA=1), Recursive (RA=0). `NOERROR` with an A record pointing at the AP for every name, except the HTTPS-probe host which gets `NXDOMAIN` (RCODE 3).
- **Dependencies:** `lwip/sockets`, `esp_netif`

## Key Features

- **Dynamic IP Resolution:** Automatically detects the current Access Point IP address using `esp_netif_get_ip_info`, ensuring correct redirection even if the network configuration changes.
- **Robust Parsing:** Implements a safe DNS name parser (`parse_dns_name`) to validate queries and prevent buffer overflows.
- **Evil Twin Optimization:** Uses specific DNS flags (`0x8500`) to mark responses as "Authoritative". This forces client devices (especially modern Android/iOS) to accept the redirection faster, improving Captive Portal detection.
- **HTTPS-probe carve-out:** `www.google.com` (the Android HTTPS connectivity probe) is answered with `NXDOMAIN` instead of the AP IP. Hijacking it would make the phone's TLS connect to the AP on `:443` get an instant RST, which some clients (notably Samsung One UI) read as "connected, no internet" and short-circuit before running the HTTP probe. Failing at DNS instead lets the HTTP probe fire and expose the portal. Matched case-insensitively via `dns_is_https_probe_host`.
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
    -   For the HTTPS-probe host: sets Flags to `0x8503` (Response + Authoritative + `NXDOMAIN`), `ancount = 0`, and returns the header + echoed question with no answer.
    -   Otherwise: sets Flags to `0x8500` (Response + Authoritative), appends the original Question section, and appends an Answer section pointing to the AP's IP address (TTL 60s).

### Configuration
- **Stack Size:** 4096 bytes (Safe for logging and network operations).
- **Socket Timeout:** 1 second (allows graceful shutdown checks).

## Captive-portal detection strategy

Getting an OS to pop the sign-in sheet needs three cooperating pieces (this mirrors both methods of the official ESP-IDF `examples/protocols/http_server/captive_portal`, which we run together):

1. **DNS hijack (this component).** Every name resolves to the AP so all traffic reaches the local server, `www.google.com` excepted (see [HTTPS-probe carve-out](#key-features)). The OS's bogus DNS-manipulation probes (`*google.com`, `google.com.onion`, random long names) resolve to the AP too, which is what a real hijacking portal does.
2. **HTTP funnelling (`http_server` + `evil_twin`).** The portal is served at `/`; every other path (the OS probes such as `/generate_204`, `/hotspot-detect.html`) falls through to the 404 handler, which replies `303 See Other` to `/` **with a body**. A bare redirect is not enough - iOS and stricter clients need content in the response to flag a portal.
3. **DHCP Option 114 / RFC 7710 (`evil_twin`).** On start the AP netif's DHCP server advertises the captive-portal URI (`http://<AP_IP>`) via `ESP_NETIF_CAPTIVEPORTAL_URI`. Modern clients read this straight from the DHCP offer and surface the portal without relying on the probe heuristics - the standards-compliant path that works around HTTPS/HSTS.

### Known limitation: Samsung One UI

The S25 (One UI) has been observed to report "connected without internet" and refuse to show the sign-in page even though it receives every signal above (DNS hijack, HTTP `200`/`302`/`303`+body, and DHCP option 114 - the last verified emitted in the DHCP offer by `dhcpserver.c`). The same firmware opens the portal correctly on non-Samsung devices, and the victim can always reach it by opening any `http://` URL in a browser. This is a device-side classification behavior, not a firmware defect; do not chase it by permuting the DNS/HTTP/DHCP responses further.
