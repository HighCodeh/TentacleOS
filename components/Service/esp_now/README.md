# ESP-NOW Service

The **ESP-NOW Service** is the low-level communication backbone for the Highboy project. It abstracts the ESP-IDF `esp_now` driver, providing a robust, connectionless messaging layer with auto-discovery, persistent peer management, and software-based security.

## Features

- **Connectionless Communication**: Uses ESP-NOW (WiFi Vendor Specific Elements) to send small packets instantly without WiFi association.
- **Auto-Discovery**: "Hello" broadcast packets allow devices to find each other.
- **Auto-Pairing (The "Cat Jump" Logic)**: Automatically registers any device from which a packet is received, allowing immediate reply without manual pairing.
- **Smart Peer Management**:
  - **Volatile (Session)**: Stores discovered peers in PSRAM (or RAM) to show who is currently online.
  - **Permanent**: Saves trusted peers to `addresses.conf` (JSON).
- **Software Security**:
  - Implements a Vigenère Cipher for message payloads to bypass ESP-NOW hardware limits (6-20 peers) while keeping packets ASCII-compatible.
  - **Secure Handshake**: Special `KEY_SHARE` packet type to exchange keys automatically.
- **Configuration Persistence**: Saves Nickname, Online Status, and Encryption Keys to `chat.conf`.

## Architecture

### Packet Structure
The service uses a packed struct to ensure consistent data alignment over the air.

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `type` | `uint8_t` | 1 byte | Packet intent (see below). |
| `nick` | `char[]` | 16 bytes | Sender's nickname. |
| `text` | `char[]` | 201 bytes | Message content or Key payload. |

### Message Types
1. **`HELLO` (0x01)**: Broadcast packet. Sent to `FF:FF:FF:FF:FF:FF`. Used for discovery.
2. **`MSG` (0x02)**: Direct message (Unicast). Encrypted if a key is set.
3. **`KEY_SHARE` (0x03)**: Handshake packet. Sent unencrypted containing the generated session key in the `text` field.

### File System Integration
The service relies on the **Assets Partition** for configuration:

1. **`/assets/config/chat/chat.conf`**:
   ```json
   {
     "nick": "Highboy_User",
     "online": true,
     "key": "SecretKey123"
   }
   ```
2. **`/assets/config/chat/addresses.conf`**:
   ```json
   [
     { "mac": "AA:BB:CC:DD:EE:FF", "name": "Friend_Device" }
   ]
   ```

## API Reference

### Initialization
```c
esp_err_t service_esp_now_init(void);
void service_esp_now_deinit(void);
```
Initializes ESP-NOW, registers callbacks, loads configuration, and allocates memory for the session list.

### Configuration
```c
esp_err_t service_esp_now_set_nick(const char *nick);
const char* service_esp_now_get_nick(void);
esp_err_t service_esp_now_set_online(bool online); // Toggle TX/RX
bool service_esp_now_is_online(void);
esp_err_t service_esp_now_set_key(const char *key); // Sets encryption key
```

### Messaging
```c
// Send HELLO to Broadcast (Discovery)
esp_err_t service_esp_now_broadcast_hello(void);

// Send Text Message (Auto-encrypts if key is set)
esp_err_t service_esp_now_send_msg(const uint8_t *target_mac, const char *text);

// Initiate Secure Handshake (Generates key if missing, sends KEY_SHARE)
esp_err_t service_esp_now_secure_pair(const uint8_t *target_mac);
```

### Peer Management
```c
// Get list of currently visible devices (from RAM/PSRAM)
int service_esp_now_get_session_peers(service_esp_now_peer_info_t *out_peers, int max_peers);

// Save a peer permanently to addresses.conf
esp_err_t service_esp_now_save_peer_to_conf(const uint8_t *mac_addr, const char *name);
```

### Callbacks
```c
typedef void (*service_esp_now_recv_cb_t)(const uint8_t *mac_addr, const service_esp_now_packet_t *data, int8_t rssi);
typedef void (*service_esp_now_send_cb_t)(const uint8_t *mac_addr, esp_now_send_status_t status);

void service_esp_now_register_recv_cb(service_esp_now_recv_cb_t cb);
void service_esp_now_register_send_cb(service_esp_now_send_cb_t cb);
```

## Security Note regarding `peer.encrypt`
We explicitly set `peer.encrypt = false` in the hardware driver.
**Reason**: ESP32 hardware encryption limits the peer list drastically (approx. 10 devices). By implementing software encryption (Vigenère) on the payload, we allow **unlimited peers** while maintaining confidentiality and enabling instant "fire-and-forget" messaging without complex hardware handshake requirements.
