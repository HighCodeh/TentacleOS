# SPI Protocol Dictionary (P4 <-> C5)

## Header Structure (4 Bytes)
| Byte | Field  | Description |
|------|--------|-------------|
| 0    | Sync   | Always `0xAA` |
| 1    | Type   | `0x01`: Command, `0x02`: Response, `0x03`: Stream |
| 2    | ID     | Function ID |
| 3    | Length | Payload Length (0-255 bytes) |

## Function ID Table

### System (0x01 - 0x0F)
| ID   | Function | Description |
|------|----------|-------------|
| 0x01 | PING     | Latency test |
| 0x02 | STATUS   | Returns C5 state (IDLE/BUSY) |

### WiFi Basic (0x10 - 0x1F)
| ID   | Function | Payload |
|------|----------|---------|
| 0x10 | SCAN     | None (Starts quick scan) |
| 0x11 | CONNECT  | [SSID (32b)][PASS (64b)] |
| 0x12 | DISCON   | None |
| 0x13 | STA_INF  | Returns connected SSID and IP |
| 0x14 | SET_AP   | [SSID][PASS][CHAN][ENAB] |

### WiFi Apps & Attacks (0x20 - 0x3F)
| ID   | Function | Payload / Description |
|------|----------|-----------------------|
| 0x20 | SCAN_AP  | Starts `ap_scanner` (Deep Scan) |
| 0x21 | SCAN_CL  | Starts `client_scanner` |
| 0x22 | BEACON   | [Mode (0:Rand, 1:Path)][Optional Path] |
| 0x23 | DEAUTH   | [BSSID (6b)][Client (6b)][Type (1b)] |
| 0x24 | FLOOD    | [Type (0:Auth, 1:Assoc)][BSSID][Ch] |
| 0x25 | SNIFFER  | [Type (0:Handshake, 1:PMKID)][Ch] |
| 0x26 | EVIL_T   | [SSID] (Starts Captive Portal) |
| 0x27 | D_DETEC  | Starts Deauth detection |
| 0x28 | PROBE_M  | Starts Probe Request monitor |
| 0x29 | SIG_MON  | [BSSID][Ch] (Monitor AP RSSI) |
| 0x2A | STOP     | Stops any active WiFi attack/app |

### Data Retrieval (0x40 - 0x4F)
| ID   | Function | Description |
|------|----------|-------------|
| 0x40 | R_COUNT  | Returns count of ready results |
| 0x41 | R_DATA   | Pulls result data chunks |

### Bluetooth Basic (0x50 - 0x5F)
| ID   | Function | Payload |
|------|----------|---------|
| 0x50 | BT_SCAN  | [Duration (4b)] |
| 0x51 | BT_CONN  | [Addr (6b)][AddrType (1b)] |
| 0x52 | BT_DISC  | None |
| 0x53 | BT_INFO  | Returns local MAC and Status |

### Bluetooth Apps & Attacks (0x60 - 0x7F)
| ID   | Function | Payload / Description |
|------|----------|-----------------------|
| 0x60 | SCANNER  | Starts `ble_scanner` (Deep Scan) |
| 0x61 | SNIFFER  | Starts `ble_sniffer` |
| 0x62 | SPAM     | [Type (0:Apple, 1:Android, 2:Samsung, 3:Windows)] |
| 0x63 | B_FLOOD  | [Addr (6b)][AddrType (1b)] (Connection Flood) |
| 0x64 | SKIMMER  | Starts Skimmer detector |
| 0x65 | TRACKER  | Starts Tracker detector (AirTag, etc) |
| 0x66 | GATT_E   | [Addr (6b)] (Explore GATT services) |
| 0x67 | APP_STP  | Stops any active BT attack/app |

### LoRa (0x80 - 0x8F)
| ID   | Function | Description |
|------|----------|-------------|
| 0x80 | RX       | Enters receive mode |
| 0x81 | TX       | Sends LoRa packet |

## SPI Pins
| Signal | ESP32-P4 | ESP32-C5 |
|--------|----------|----------|
| SCLK   | GPIO 20  | GPIO 6   |
| MOSI   | GPIO 21  | GPIO 7   |
| MISO   | GPIO 22  | GPIO 2   |
| CS     | GPIO 23  | GPIO 10  |
| IRQ    | GPIO 02  | GPIO 03  |