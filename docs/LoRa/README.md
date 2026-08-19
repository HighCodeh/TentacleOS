# LoRa Application

This component implements the LoRa mesh application layer on top of the `sx1262` driver. It hosts two full mesh protocol stacks - **Meshtastic** and **MeshCore** - behind a unified session manager, plus an **RNode**-compatible KISS modem. It also bridges a companion phone app over BLE and drives the on-device chat UI.

## Overview

- **Location:** `components/Applications/LoRa/`
- **Sub-modules:** `session/`, `meshtastic/`, `meshcore/`, `rnode/`
- **Radio:** single SX1262 (see `docs/sx1262/README.md`)
- **UI screens:** `components/Applications/ui/screens/lora/`
- **BLE / Wi-Fi:** terminated on the **C5 co-processor**, reached over the SPI bridge

## Single-Owner Radio

The SX1262 is a single-owner resource. Meshtastic, MeshCore, and RNode each register their own radio callbacks and cannot run at the same time; whichever stack starts first owns the radio. **Switching protocols requires a reboot** - there is no clean whole-stack teardown. The session manager enforces this: once a protocol is running, starting a different one is refused.

## Split-Chip Architecture (P4 + C5)

The design is consistent across both mesh stacks:

- **The P4** owns the protocol logic, the radio, and the PhoneAPI/command state.
- **The C5** owns NimBLE (GATT service, pairing, CCCD subscribe) and the Wi-Fi/TCP server.

The two communicate over the **SPI bridge**. Each mesh stack has a "phone bridge" on the P4 that:

- Reassembles chunked StreamAPI/companion frames (`spi_mesh_chunk_hdr_t`: seq / chunk_idx / total_chunks / flags) arriving from the C5 and forwards them to the P4 PhoneAPI.
- Fragments outbound frames and pushes them to the C5.
- Polls a cached C5 status struct (`spi_mesh_status_t` / `spi_mcore_status_t`: `ble_connected`, `ble_subscribed`, `tcp_clients`, `logradio_subscribed`).
- Runs a **want-vs-actual reconcile loop** so BLE re-arms automatically after a C5 reboot.

**"Connected" vs "linked":** `ble_connected` is the raw BLE link (true before the pairing PIN is even shown). The UI/session truth is **"linked" = subscribed after encryption** - the phone has enabled notifications on the notify characteristic (a CCCD subscribe on the C5). The session and UI gate on the *subscribed* predicate, not the *connected* one.

## Session Manager (`session/`)

`lora_session` is a thin, protocol-agnostic facade the chat hub reads. It never calls the SX1262 directly - each stack's `app_start` brings the radio up. The session layer:

- Tracks which protocol owns the radio.
- Holds a **local chat ring** of 24 messages (in PSRAM), guarded by a mutex, with a monotonic sequence used by a cursor-based read API.
- Maps a unified node/contact list onto either Meshtastic's nodedb or MeshCore's contacts.
- Fronts the phone-bridge connect/subscribe state.

### Types

```c
typedef enum { LORA_PROTO_NONE = 0, LORA_PROTO_MESHTASTIC, LORA_PROTO_MESHCORE } lora_proto_t;
typedef struct { bool outgoing; char who[24]; char text[160]; } lora_msg_t;
typedef struct { char name[32]; int16_t rssi; float snr; } lora_node_t;
```

### API

```c
esp_err_t    lora_session_start(lora_proto_t proto);
lora_proto_t lora_session_active(void);
esp_err_t    lora_session_send_text(const char *text);
uint16_t     lora_session_msg_count(void);
uint16_t     lora_session_msg_since(uint32_t *io_seq, lora_msg_t *out, uint16_t max);
uint16_t     lora_session_node_count(void);
bool         lora_session_node_get(uint16_t idx, lora_node_t *out);
void         lora_session_on_rx_text(const char *who, const char *text);
bool         lora_session_app_connected(void);
esp_err_t    lora_session_app_connect(void);
```

- `lora_session_start` is idempotent for the same protocol; a request for a different protocol while one is running returns `ESP_ERR_INVALID_STATE`.
- `lora_session_send_text` sends a broadcast/public-channel text on the active stack (Meshtastic `0xFFFFFFFF`, MeshCore public channel `0`) and, on success, appends it to the ring as outgoing ("me").
- `lora_session_msg_since` copies messages newer than a caller-held cursor under a single lock; passing `*io_seq == 0` snaps to the oldest retained message.
- `lora_session_on_rx_text` is called by the backend RX taps from the mesh poll task (not the LVGL thread).
- `lora_session_app_connected` maps to the active stack's **`_is_subscribed()`** predicate (linked, not merely connected). `lora_session_app_connect` maps to `_phone_bridge_ble_start()`.

## Meshtastic Stack (`meshtastic/`)

Entry point: `esp_err_t meshtastic_app_start(void)` (call **after** `bridge_manager_init()`).

Bring-up derives `node_num` from the MAC, loads persisted preset/region (default LONG_FAST + US), brings up the SX1262 (region center frequency, preset SF/BW/CR), starts the IRQ task, initializes PKI (X25519 / Curve25519 via mbedTLS), the nodedb, the module hub, the mesh core, the PhoneAPI, and the phone bridge, then starts a poll task (mesh poll at 50 ms, module tick at 1 Hz) and requests C5 BLE advertising.

### Mesh core (`meshtastic_mesh`)

```c
esp_err_t meshtastic_mesh_init(uint32_t node_num);
esp_err_t meshtastic_mesh_start(void);
void      meshtastic_mesh_stop(void);
esp_err_t meshtastic_mesh_send(const uint8_t *pb_data, uint16_t pb_len);
void      meshtastic_mesh_poll(void);
esp_err_t meshtastic_mesh_send_nodeinfo(void);
esp_err_t meshtastic_mesh_send_text(const char *text, uint32_t to);
esp_err_t meshtastic_mesh_send_data(uint32_t to, uint8_t channel, uint8_t hop_limit,
                                    uint8_t portnum, const uint8_t *payload, uint16_t plen,
                                    uint32_t request_id, bool want_ack, bool want_response);
bool      meshtastic_mesh_retry_ack(uint32_t pkt_id, bool is_implicit);
```

### Modules (`mt_modules` + `mt_mod_*`)

The module hub dispatches decoded packets by portnum. Modules present: Admin, NodeInfo, Position, Text, Routing, TraceRoute, Telemetry, KeyVerify, NeighborInfo. Each exposes `_init(node_num)` and `_on_received(meta, payload, len)`; timer-driven ones add a tick.

```c
esp_err_t mt_modules_init(uint32_t node_num);
void      mt_modules_dispatch(const mt_packet_meta_t *meta, const uint8_t *data, uint16_t len);
void      mt_modules_tick(void);   // 1 Hz
bool      mt_parse_data(const uint8_t *data, uint16_t len, uint32_t *out_portnum,
                        const uint8_t **out_payload, uint16_t *out_payload_len,
                        uint32_t *out_request_id);
```

Per-packet metadata (`mt_packet_meta_t`) carries `from`, `to`, `id`, `channel`, `hop_limit`, `hop_start`, `rssi_dbm`, `snr_db`, `want_ack`, `want_response`, `request_id`.

Supporting modules: `meshtastic_nodedb` (node database, favorites/ignore/mute, next-hop learning), `meshtastic_channels` (channel PSKs and hashes), `meshtastic_pki` / `meshtastic_crypto_pki` (Curve25519 encrypt/decrypt), `meshtastic_pkt_history` (dedup / relayer tracking), `meshtastic_mqtt`, `meshtastic_wifi`, `meshtastic_regions`, `meshtastic_presets`, `meshtastic_roles`, plus vendored `unishox2` text compression.

### Traceroute (`mt_mod_traceroute`)

A traceroute request is a zero-payload Data packet on the TraceRoute port with `want_response` set.

```c
#define MT_TRACE_MAX_HOPS 8
void mt_mod_traceroute_init(uint32_t node_num);
void mt_mod_traceroute_on_received(const mt_packet_meta_t *meta, const uint8_t *payload, uint16_t len);
void mt_mod_traceroute_start(uint32_t to);
bool mt_mod_traceroute_is_pending(void);
bool mt_mod_traceroute_get_result(uint32_t *out_hops, int *out_count, uint32_t *out_target);
```

- `mt_mod_traceroute_start(to)` marks a pending request, clears any prior result, and sends the request via `meshtastic_mesh_send_data(...)`.
- `mt_mod_traceroute_on_received` decodes the `RouteDiscovery` protobuf hop list. If it matches the pending request (reply addressed to us, from the target), the hops are captured into module-static state and `_get_result` reports them. If this node is itself the request destination, it appends its own node number to the route and sends the reply. Non-terminal hop forwarding relies on the mesh core's flood rebroadcast.
- There is no dedicated result struct; results are returned through the out-parameters of `mt_mod_traceroute_get_result` and held internally until the next request. The traceroute screen is `lora_traceroute_ui`.

### PhoneAPI (`meshtastic_phoneapi`)

Implements the official PhoneAPI handshake as a state machine (`phoneapi_state_t`: MyInfo -> Metadata -> DeviceUIConfig -> Channels -> Config -> ModuleConfig -> FileManifest -> own NodeInfo -> other NodeInfos -> complete-id -> packets).

```c
esp_err_t phoneapi_init(uint32_t node_num);
esp_err_t phoneapi_on_toradio(const uint8_t *pb_data, uint16_t pb_len);
uint16_t  phoneapi_poll_fromradio(uint8_t *out_buf, uint16_t max_len);
bool      phoneapi_has_data(void);
void      phoneapi_push_packet(const uint8_t *mp_bytes, uint16_t mp_len);
void      phoneapi_disconnect(void);
```

### BLE phone-app connectivity

On the P4, `meshtastic_ble` is a thin shim forwarding to `meshtastic_phone_bridge`; the real NimBLE host and GATT service run on the C5.

```c
esp_err_t meshtastic_phone_bridge_init(void);
esp_err_t meshtastic_phone_bridge_ble_start(void);
esp_err_t meshtastic_phone_bridge_ble_stop(void);
bool      meshtastic_phone_bridge_is_connected(void);   // ble_connected || tcp_clients
bool      meshtastic_phone_bridge_is_subscribed(void);  // ble_subscribed || tcp_clients
```

- **NimBLE arbitration:** the P4 never owns NimBLE. `_ble_start()` sends a BLE-init request to the C5 over the SPI bridge; a notify task reconciles desired vs actual C5 state (re-arming BLE after a C5 reboot) and polls status. Because only one LoRa stack runs at a time, only one bridge ever issues BLE-init; cross-app BLE arbitration (mesh vs host-link vs HID) lives on the C5, which avoids tearing NimBLE out from under an active app.
- **Pairing PIN:** for Meshtastic the passkey is generated randomly on the C5 per pairing (display-only IO capability, MITM + bonding) and logged there. It is **not** shown on the P4 chat screen.
- **Linked-on-subscribe gating:** `_is_connected()` is true at the raw BLE link; `_is_subscribed()` is true only after the phone subscribes (CCCD notify enabled on the FromNum characteristic, after encryption). Fromradio data is only pushed to the phone while a BLE client is connected or a TCP client is present.

Other module surfaces (used by the UI/config screens): `meshtastic_mqtt` (`_init/_publish/_is_connected`), `meshtastic_wifi`, region/preset/role selection (`mt_region_*`, `mt_preset_*`, `mt_role_*` with `MT_ROLE_ROUTER` / `MT_ROLE_CLIENT`).

## MeshCore Stack (`meshcore/`)

Entry point: `esp_err_t meshcore_app_start(void)` (call **after** `bridge_manager_init()`).

Bring-up initializes libsodium, brings up the SX1262 with MeshCore defaults (915 MHz, SF10, BW 250 kHz, CR 4/5, +20 dBm, preamble 8), loads or creates an Ed25519 identity (32-byte seed persisted in NVS, default node name "Highboy"), initializes the core with five router callbacks, the companion PhoneAPI, and the phone bridge, then starts a poll task (50 ms) and requests C5 BLE advertising.

The five router callbacks bridge radio events to both the phone and the local ring: new advert, group text (`lora_session_on_rx_text("ch%u", ...)`), direct message (`lora_session_on_rx_text(contact-name or "dm", ...)`), ACK confirmation, and path update.

### Core (`meshcore`)

Constants include `MESHCORE_MAX_CONTACTS 32`, `MESHCORE_MAX_CHANNELS 8`, `MESHCORE_PUBLIC_CHANNEL 0`, `MESHCORE_TEXT_MAX 160`. Key surface used by the app/session:

- `meshcore_init`, `meshcore_start` / `_stop` / `_poll`
- `meshcore_set_node_name`, `meshcore_set_advert_latlon`, `meshcore_send_advert` / `_throttled` (10 s cooldown)
- `meshcore_channel_get` / `_set`, `meshcore_send_grp_txt(channel_idx, text)`
- Contacts: `meshcore_contacts_array`, `meshcore_contacts_count`, `meshcore_contact_find` / `_find_by_hash` / `_upsert` / `_remove` / `_reset_path`
- `meshcore_send_direct_msg(peer_pub_key, text, timestamp, attempt, out_expected_ack)` - X25519 ECDH + AES-128 + HMAC-SHA256
- `meshcore_set_radio_params` / `_get_radio_prefs`, `meshcore_set_unix_time` / `_get_unix_time`, `meshcore_set_relay` / `_get_relay`

Identity/contact/channel structs (`meshcore_identity_t`, `meshcore_contact_t`, `meshcore_channel_t`) are defined in `meshcore.h`.

### Companion PhoneAPI (`meshcore_phoneapi`)

Unlike Meshtastic's FSM, the outbound path is a **registered callback** invoked synchronously from command handlers and push helpers.

```c
esp_err_t meshcore_phoneapi_init(void);                 // loads/generates the 6-digit PIN from NVS
void      meshcore_phoneapi_set_outbound(meshcore_phoneapi_outbound_cb_t cb, void *ctx);
uint32_t  meshcore_phoneapi_get_pin(void);              // 6-digit BLE pairing PIN
void      meshcore_phoneapi_on_disconnect(void);
void      meshcore_phoneapi_on_inbound(const uint8_t *buf, uint16_t len);   // dispatches CMD_* opcodes
void      meshcore_phoneapi_push_new_advert(const meshcore_contact_t *contact);
void      meshcore_phoneapi_push_channel_msg(uint8_t ch, uint8_t path_len, uint32_t ts, int8_t snr, const char *text);
void      meshcore_phoneapi_push_contact_msg(const uint8_t peer_pub_key[32], uint8_t path_len,
                                             uint8_t txt_type, uint32_t ts, int8_t snr, const char *text);
void      meshcore_phoneapi_push_send_confirmed(uint32_t ack_crc, uint8_t snr);
void      meshcore_phoneapi_push_path_updated(const meshcore_contact_t *contact);
```

### Phone bridge (`meshcore_phone_bridge`)

```c
esp_err_t meshcore_phone_bridge_init(const char *name_prefix);  // NULL -> "Highboy-MC"
void      meshcore_phone_bridge_stop(void);
esp_err_t meshcore_phone_bridge_ble_start(void);
esp_err_t meshcore_phone_bridge_ble_stop(void);
bool      meshcore_phone_bridge_is_connected(void);   // ble_connected
bool      meshcore_phone_bridge_is_subscribed(void);  // ble_subscribed
```

Same C5-terminated model as Meshtastic (chunked reassembly, status polling task, reconcile loop that survives C5 reboots). Two differences worth noting:

- **Pairing PIN:** MeshCore uses a **fixed, device-persisted 6-digit PIN** (from `meshcore_phoneapi_get_pin()`), passed to the C5 at BLE-init and injected on the passkey-display event. This PIN **is** rendered on the P4 chat connect screen.
- **Linked-on-subscribe gating:** `ble_connected` flips at the raw link; `ble_subscribed` flips only after encryption and the app subscribing to the notify characteristic (CCCD subscribe on the TX characteristic, on the C5).

## Chat UI (`ui/screens/lora/`)

`lora_chat_ui` is a single-screen state machine.

```c
void ui_lora_chat_open(void);        // opens on the protocol picker
void ui_lora_chat_open_chat(void);   // opens directly on the chat conversation
```

Views: **Protocol picker** (MeshCore / Meshtastic) -> **Home** -> { Connect App, Nodes, Chat, Configs }. Home also links out to the sibling screens: Channels, Position, Telemetry, Secure DM, Traceroute (each its own screen under `ui/screens/lora/`).

- **Home** shows region + preset (Meshtastic only) and a status banner driven by the linked state.
- **Nodes** shows either a radial "map" of node pins with RSSI-colored links or a scrolling list, both fed by `lora_session_node_count()` / `lora_session_node_get()`.
- **Chat** renders message bubbles ("MESH BROADCAST" for Meshtastic, "PUBLIC CHANNEL" for MeshCore). An LVGL timer drains new messages every 500 ms via `lora_session_msg_since(...)`; the on-screen keyboard submits through `lora_session_send_text(...)`.
- **Configs** (Meshtastic only) offers Region / Preset selectors and a Router-mode toggle.
- **Connect** calls `lora_session_app_connect()`, polls `lora_session_app_connected()` every second, and shows the pairing PIN (MeshCore only).

Starting a protocol runs `lora_session_start(proto)` on a background task pinned to the radio core; if a different protocol already owns the radio the UI prompts to reboot to switch.

## RNode Modem (`rnode/`)

`rnode/` shares the SX1262 driver but is **not** a mesh protocol - it is an **RNode-compatible KISS/serial modem** (Reticulum RNode firmware emulation, reporting v1.52). A host running Reticulum drives the radio over KISS-framed serial. It is mutually exclusive with the mesh stacks (same single-owner radio), but it is started independently of `lora_session` (it is not one of the `lora_proto_t` values).

```c
esp_err_t rnode_init(const sx1262_hal_t *hal);   // KISS engine + serial + SX1262 hookup
esp_err_t rnode_start(void);                      // IRQ task + continuous RX
void      rnode_poll(void);                        // drains serial, runs KISS decoder, dispatches
const rnode_radio_cfg_t *rnode_get_radio_cfg(void);
const rnode_stats_t     *rnode_get_stats(void);
```

Defaults: 915 MHz, BW 125 kHz, SF8, CR 4/5, +17 dBm. The on-device screen is `lora_rnode_ui`.
