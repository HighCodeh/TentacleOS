// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

// This file is the P4<->C5 wire contract and MUST stay byte-identical to its twin
// in the other firmware (firmware_p4 and firmware_c5 each keep a copy at
// components/Service/spi_bridge/include/spi_protocol.h). Both chips parse the same
// bytes, so any id/struct edited on one side has to be copied to the other or the
// two ends interpret the same frame differently. The P4 is the superset: ops it
// owns but the C5 does not implement (screen share, port scan, ...) still reserve
// their number here, so a given id never means two different things across the bus.

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_rom_crc.h"

#define SPI_SYNC_BYTE 0xAA
// Capped at 255: the header's `length` field is a uint8_t, so 256 was never
// representable ((uint8_t)256 == 0). For a RESPONSE, `length` also counts the
// status byte, so its data maxes at 254 (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE).
#define SPI_MAX_PAYLOAD      255
#define SPI_RESP_STATUS_SIZE 1

// Wire-protocol version. Bump on ANY change to the contract below (an id, a
// struct, or the header layout). The P4 reads the C5's value at bridge init via
// SPI_ID_SYSTEM_PROTO_VERSION and flags a mismatch loudly, so two copies of this
// header that drifted are caught at boot instead of silently misparsing frames.
#define SPI_PROTOCOL_VERSION 6

#define SPI_BT_SPAM_ITEM_LEN          32
#define SPI_BT_SPAM_LIST_MAX          64
#define SPI_WIFI_SNIFFER_FILENAME_MAX 96

// Special indices for SPI_ID_SYSTEM_DATA
#define SPI_DATA_INDEX_COUNT        0xFFFF
#define SPI_DATA_INDEX_STATS        0xEEEE
#define SPI_DATA_INDEX_DEAUTH_COUNT 0xDDDD

/**
 * @brief SPI message types.
 */
typedef enum { SPI_TYPE_CMD = 0x01, SPI_TYPE_RESP = 0x02, SPI_TYPE_STREAM = 0x03 } spi_type_t;

/**
 * @brief Bridge handshake mode (physical layer only; the wire protocol is the
 * same in both). IRQ: the C5 pulses a GPIO when a response/stream frame is
 * armed (default; needs the IRQ trace). POLL: no IRQ line, so the P4 re-clocks
 * the bus until the slave answers with a valid frame.
 */
typedef enum { SPI_BRIDGE_MODE_IRQ = 0, SPI_BRIDGE_MODE_POLL = 1 } spi_bridge_mode_t;

/**
 * @brief SPI command categories (subsystems).
 *
 * Carried as the `category` byte of the frame header. The C5 routes a command
 * to a dispatcher by this byte alone; the `op` byte selects the operation
 * within the category.
 */
typedef enum {
  SPI_CAT_SYSTEM = 0x00,
  SPI_CAT_WIFI = 0x01,
  SPI_CAT_BT = 0x02,
  SPI_CAT_LORA = 0x03,
  SPI_CAT_MESH = 0x04,     // Meshtastic phone bridge
  SPI_CAT_MCORE = 0x05,    // MeshCore phone bridge
  SPI_CAT_HOST = 0x06,     // Companion host-link BLE relay
  SPI_CAT_SCREEN = 0x07,   // P4-native screen sharing over the host link (USB)
  SPI_CAT_IR = 0x08,       // P4-native IR TX/RX/torch over the host link (USB)
  SPI_CAT_SUBGHZ = 0x09,   // P4-native Sub-GHz (CC1101) over the host link
  SPI_CAT_AUDIO = 0x0A,    // P4-native audio (speaker/tone/chime) over the host link
  SPI_CAT_LED = 0x0B,      // P4-native RGB status LED over the host link
  SPI_CAT_BADUSB = 0x0C,   // P4-native BadUSB (USB-HID); companion host-link only
  SPI_CAT_LORACFG = 0x0D,  // P4-native LoRa radio config; companion host-link only
  SPI_CAT_LORACHAT = 0x0E, // P4-native LoRa chat; companion host-link only
  SPI_CAT_SESSION = 0xFF
} spi_cat_t;

/** Pack a (category, op) pair into a 16-bit command identifier. */
#define SPI_CMD(cat, op) ((uint16_t)(((uint8_t)(cat) << 8) | (uint8_t)(op)))
/** Extract the category byte from a packed command identifier. */
#define SPI_CMD_CAT(cmd) ((uint8_t)((cmd) >> 8))
/** Extract the op byte from a packed command identifier. */
#define SPI_CMD_OP(cmd) ((uint8_t)((cmd) & 0xFF))

/**
 * @brief SPI function/command identifiers.
 *
 * Each value packs its category (high byte) and op (low byte) via SPI_CMD().
 */
typedef enum {
  // System
  SPI_ID_SYSTEM_PING = SPI_CMD(SPI_CAT_SYSTEM, 0x01),
  SPI_ID_SYSTEM_STATUS = SPI_CMD(SPI_CAT_SYSTEM, 0x02),
  SPI_ID_SYSTEM_REBOOT = SPI_CMD(SPI_CAT_SYSTEM, 0x03),
  SPI_ID_SYSTEM_VERSION = SPI_CMD(SPI_CAT_SYSTEM, 0x04),
  SPI_ID_SYSTEM_DATA = SPI_CMD(SPI_CAT_SYSTEM, 0x05),
  SPI_ID_SYSTEM_STREAM = SPI_CMD(SPI_CAT_SYSTEM, 0x06),
  SPI_ID_SYSTEM_LOG = SPI_CMD(SPI_CAT_SYSTEM, 0x07), // C5→P4 stream: log lines [level u8][utf-8]
  SPI_ID_SYSTEM_ENTER_DOWNLOAD =
      SPI_CMD(SPI_CAT_SYSTEM, 0x08), // P4→C5: reboot into ROM download mode (serial-flash recovery)
  SPI_ID_SYSTEM_OTA_BEGIN =
      SPI_CMD(SPI_CAT_SYSTEM,
              0x09), // P4→C5: begin app OTA. Payload = spi_ota_begin_t (u32 size + u8
                     // transport). C5 erases; bytes then arrive over SPI (OTA_DATA) or UART.
  SPI_ID_SYSTEM_OTA_STATUS = SPI_CMD(
      SPI_CAT_SYSTEM, 0x0A), // P4→C5: poll OTA progress. Response payload = spi_ota_status_t.
  SPI_ID_SYSTEM_OTA_DATA = SPI_CMD(
      SPI_CAT_SYSTEM, 0x0B), // P4→C5: one firmware chunk (SPI transport). Written sequentially; the
                             // C5 finalizes and reboots once bytes_written reaches the image size.
  SPI_ID_SYSTEM_INFO =
      SPI_CMD(SPI_CAT_SYSTEM, 0x0C), // P4→C5: read chip info. Response payload = spi_sys_info_t.
  SPI_ID_SYSTEM_PROTO_VERSION = SPI_CMD(
      SPI_CAT_SYSTEM, 0x0D), // P4→C5: read the C5's SPI_PROTOCOL_VERSION. Response payload = u16.

  // Companion file ops. P4-local host-link commands (the P4 owns flash + SD);
  // listed here only so the app and P4 share one id space. Never relayed to C5.
  SPI_ID_FILE_LIST = SPI_CMD(SPI_CAT_SYSTEM, 0x40),
  SPI_ID_FILE_STAT = SPI_CMD(SPI_CAT_SYSTEM, 0x41),
  SPI_ID_FILE_READ = SPI_CMD(SPI_CAT_SYSTEM, 0x42),
  SPI_ID_FILE_WRITE = SPI_CMD(SPI_CAT_SYSTEM, 0x43),
  SPI_ID_FILE_DELETE = SPI_CMD(SPI_CAT_SYSTEM, 0x44),
  SPI_ID_FILE_MKDIR = SPI_CMD(SPI_CAT_SYSTEM, 0x45),

  // Companion device state + settings + console exec. Also P4-local host-link
  // commands (never relayed to C5); ids shared so the app and P4 agree.
  SPI_ID_SYSTEM_DEVICE_STATE = SPI_CMD(SPI_CAT_SYSTEM, 0x46),
  SPI_ID_SYSTEM_CONSOLE_EXEC = SPI_CMD(SPI_CAT_SYSTEM, 0x47),
  SPI_ID_SYSTEM_GET_SETTINGS = SPI_CMD(SPI_CAT_SYSTEM, 0x48),
  SPI_ID_SYSTEM_SET_SETTINGS = SPI_CMD(SPI_CAT_SYSTEM, 0x49),
  // P4→C5: device power state (payload = spi_power_state_t). Lets the C5 drop its
  // radio when the P4 is idle/asleep instead of running full RX all the time.
  SPI_ID_SYSTEM_POWER_STATE = SPI_CMD(SPI_CAT_SYSTEM, 0x4A),
  // Config cache: the P4 (master) pushes config files to the C5, hash-gated so an
  // unchanged file is never re-sent. Config ids are spi_cfg_id_t.
  SPI_ID_SYSTEM_CFG_HASH = SPI_CMD(SPI_CAT_SYSTEM, 0x50),   // P4→C5: read C5's stored hash [id u8]→[hash u32]
  SPI_ID_SYSTEM_CFG_BEGIN = SPI_CMD(SPI_CAT_SYSTEM, 0x51),  // P4→C5: begin push (spi_cfg_begin_t)
  SPI_ID_SYSTEM_CFG_CHUNK = SPI_CMD(SPI_CAT_SYSTEM, 0x52),  // P4→C5: [id u8][offset u32][bytes]
  SPI_ID_SYSTEM_CFG_COMMIT = SPI_CMD(SPI_CAT_SYSTEM, 0x53), // P4→C5: [id u8] finalize (write + store hash)
  // P4-local companion op: switch the active UI theme by name (never relayed).
  SPI_ID_SYSTEM_SET_THEME = SPI_CMD(SPI_CAT_SYSTEM, 0x4C),
  // P4-local companion settings: structured per-section get/set of the device
  // config (tos_config), applied live. Payload begins with a spi_config_section_t.
  SPI_ID_SYSTEM_CONFIG_GET = SPI_CMD(SPI_CAT_SYSTEM, 0x4B),
  SPI_ID_SYSTEM_CONFIG_SET = SPI_CMD(SPI_CAT_SYSTEM, 0x4D),

  // WiFi Basic
  SPI_ID_WIFI_SCAN = SPI_CMD(SPI_CAT_WIFI, 0x10),
  SPI_ID_WIFI_SCAN_STATUS = SPI_CMD(SPI_CAT_WIFI, 0x50), // P4 polls this: 1 = scan running
  SPI_ID_WIFI_CONNECT = SPI_CMD(SPI_CAT_WIFI, 0x11),
  SPI_ID_WIFI_DISCONNECT = SPI_CMD(SPI_CAT_WIFI, 0x12),
  SPI_ID_WIFI_GET_STA_INFO = SPI_CMD(SPI_CAT_WIFI, 0x13),
  SPI_ID_WIFI_SET_AP = SPI_CMD(SPI_CAT_WIFI, 0x14),
  SPI_ID_WIFI_START = SPI_CMD(SPI_CAT_WIFI, 0x15),
  SPI_ID_WIFI_STOP = SPI_CMD(SPI_CAT_WIFI, 0x16),
  SPI_ID_WIFI_SAVE_AP_CONFIG = SPI_CMD(SPI_CAT_WIFI, 0x17),
  SPI_ID_WIFI_SET_ENABLED = SPI_CMD(SPI_CAT_WIFI, 0x18),
  SPI_ID_WIFI_SET_AP_PASSWORD = SPI_CMD(SPI_CAT_WIFI, 0x19),
  SPI_ID_WIFI_SET_AP_MAX_CONN = SPI_CMD(SPI_CAT_WIFI, 0x1A),
  SPI_ID_WIFI_SET_AP_IP = SPI_CMD(SPI_CAT_WIFI, 0x1B),
  SPI_ID_WIFI_PROMISC_START = SPI_CMD(SPI_CAT_WIFI, 0x1C),
  SPI_ID_WIFI_PROMISC_STOP = SPI_CMD(SPI_CAT_WIFI, 0x1D),
  SPI_ID_WIFI_CH_HOP_START = SPI_CMD(SPI_CAT_WIFI, 0x1E),
  SPI_ID_WIFI_CH_HOP_STOP = SPI_CMD(SPI_CAT_WIFI, 0x1F),

  // WiFi Applications & Attacks
  SPI_ID_WIFI_APP_SCAN_AP = SPI_CMD(SPI_CAT_WIFI, 0x20),
  SPI_ID_WIFI_APP_SCAN_CLIENT = SPI_CMD(SPI_CAT_WIFI, 0x21),
  SPI_ID_WIFI_APP_BEACON_SPAM = SPI_CMD(SPI_CAT_WIFI, 0x22),
  SPI_ID_WIFI_APP_DEAUTHER = SPI_CMD(SPI_CAT_WIFI, 0x23),
  SPI_ID_WIFI_APP_FLOOD = SPI_CMD(SPI_CAT_WIFI, 0x24),
  SPI_ID_WIFI_APP_SNIFFER = SPI_CMD(SPI_CAT_WIFI, 0x25),
  SPI_ID_WIFI_APP_EVIL_TWIN = SPI_CMD(SPI_CAT_WIFI, 0x26),
  SPI_ID_WIFI_APP_DEAUTH_DET = SPI_CMD(SPI_CAT_WIFI, 0x27),
  SPI_ID_WIFI_APP_PROBE_MON = SPI_CMD(SPI_CAT_WIFI, 0x28),
  SPI_ID_WIFI_APP_SIGNAL_MON = SPI_CMD(SPI_CAT_WIFI, 0x29),
  SPI_ID_WIFI_APP_SCAN_AP_DETAIL = SPI_CMD(SPI_CAT_WIFI, 0x2A),
  SPI_ID_WIFI_SNIFFER_SET_SNAPLEN = SPI_CMD(SPI_CAT_WIFI, 0x2B),
  SPI_ID_WIFI_SNIFFER_SET_VERBOSE = SPI_CMD(SPI_CAT_WIFI, 0x2C),
  SPI_ID_WIFI_SNIFFER_SAVE_FLASH = SPI_CMD(SPI_CAT_WIFI, 0x2D),
  SPI_ID_WIFI_SNIFFER_SAVE_SD = SPI_CMD(SPI_CAT_WIFI, 0x2E),
  SPI_ID_WIFI_SNIFFER_FREE_BUFFER = SPI_CMD(SPI_CAT_WIFI, 0x2F),
  SPI_ID_WIFI_SNIFFER_STREAM_SD = SPI_CMD(SPI_CAT_WIFI, 0x30),
  SPI_ID_WIFI_SNIFFER_CLEAR_PMKID = SPI_CMD(SPI_CAT_WIFI, 0x31),
  SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID = SPI_CMD(SPI_CAT_WIFI, 0x32),
  SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE = SPI_CMD(SPI_CAT_WIFI, 0x33),
  SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID = SPI_CMD(SPI_CAT_WIFI, 0x34),
  SPI_ID_WIFI_DEAUTH_STATUS = SPI_CMD(SPI_CAT_WIFI, 0x35),
  SPI_ID_WIFI_DEAUTH_SEND_RAW = SPI_CMD(SPI_CAT_WIFI, 0x36),
  SPI_ID_WIFI_ASSOC_REQUEST = SPI_CMD(SPI_CAT_WIFI, 0x37),
  SPI_ID_WIFI_DEAUTH_SEND_FRAME = SPI_CMD(SPI_CAT_WIFI, 0x38),
  SPI_ID_WIFI_DEAUTH_SEND_BROADCAST = SPI_CMD(SPI_CAT_WIFI, 0x39),
  SPI_ID_WIFI_TARGET_SCAN_START = SPI_CMD(SPI_CAT_WIFI, 0x3A),
  SPI_ID_WIFI_TARGET_SCAN_STATUS = SPI_CMD(SPI_CAT_WIFI, 0x3B),
  SPI_ID_WIFI_TARGET_SAVE_FLASH = SPI_CMD(SPI_CAT_WIFI, 0x3C),
  SPI_ID_WIFI_TARGET_SAVE_SD = SPI_CMD(SPI_CAT_WIFI, 0x3D),
  SPI_ID_WIFI_TARGET_FREE = SPI_CMD(SPI_CAT_WIFI, 0x3E),
  SPI_ID_WIFI_PROBE_SAVE_FLASH = SPI_CMD(SPI_CAT_WIFI, 0x3F),
  SPI_ID_WIFI_PROBE_SAVE_SD = SPI_CMD(SPI_CAT_WIFI, 0x40),
  SPI_ID_WIFI_EVIL_TWIN_TEMPLATE = SPI_CMD(SPI_CAT_WIFI, 0x41),
  SPI_ID_WIFI_EVIL_TWIN_HAS_PASSWORD = SPI_CMD(SPI_CAT_WIFI, 0x42),
  SPI_ID_WIFI_EVIL_TWIN_GET_PASSWORD = SPI_CMD(SPI_CAT_WIFI, 0x43),
  SPI_ID_WIFI_EVIL_TWIN_RESET_CAPTURE = SPI_CMD(SPI_CAT_WIFI, 0x44),
  SPI_ID_WIFI_CLIENT_SAVE_FLASH = SPI_CMD(SPI_CAT_WIFI, 0x45),
  SPI_ID_WIFI_CLIENT_SAVE_SD = SPI_CMD(SPI_CAT_WIFI, 0x46),
  SPI_ID_WIFI_AP_SAVE_FLASH = SPI_CMD(SPI_CAT_WIFI, 0x47),
  SPI_ID_WIFI_AP_SAVE_SD = SPI_CMD(SPI_CAT_WIFI, 0x48),
  SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE = SPI_CMD(SPI_CAT_WIFI, 0x49),
  SPI_ID_WIFI_PORT_SCAN_TARGET_LIST = SPI_CMD(SPI_CAT_WIFI, 0x4A),
  SPI_ID_WIFI_PORT_SCAN_NETWORK = SPI_CMD(SPI_CAT_WIFI, 0x4B),
  SPI_ID_WIFI_PORT_SCAN_CIDR = SPI_CMD(SPI_CAT_WIFI, 0x4C),
  SPI_ID_WIFI_PORT_SCAN_STOP = SPI_CMD(SPI_CAT_WIFI, 0x4D),
  SPI_ID_WIFI_GET_MAC = SPI_CMD(SPI_CAT_WIFI, 0x4E),
  SPI_ID_WIFI_GET_IP_INFO = SPI_CMD(SPI_CAT_WIFI, 0x4F),
  SPI_ID_WIFI_EVIL_TWIN_TMPL_BEGIN = SPI_CMD(SPI_CAT_WIFI, 0xA0),
  SPI_ID_WIFI_EVIL_TWIN_TMPL_CHUNK = SPI_CMD(SPI_CAT_WIFI, 0xA1),

  // Bluetooth Basic
  SPI_ID_BT_SCAN = SPI_CMD(SPI_CAT_BT, 0x50),
  SPI_ID_BT_SCAN_STATUS = SPI_CMD(SPI_CAT_BT, 0x7F), // P4 polls this: 1 = scan running
  SPI_ID_BT_CONNECT = SPI_CMD(SPI_CAT_BT, 0x51),
  SPI_ID_BT_DISCONNECT = SPI_CMD(SPI_CAT_BT, 0x52),
  SPI_ID_BT_GET_INFO = SPI_CMD(SPI_CAT_BT, 0x53),
  SPI_ID_BT_INIT = SPI_CMD(SPI_CAT_BT, 0x54),
  SPI_ID_BT_DEINIT = SPI_CMD(SPI_CAT_BT, 0x55),
  SPI_ID_BT_START = SPI_CMD(SPI_CAT_BT, 0x56),
  SPI_ID_BT_STOP = SPI_CMD(SPI_CAT_BT, 0x57),
  SPI_ID_BT_SET_RANDOM_MAC = SPI_CMD(SPI_CAT_BT, 0x58),
  SPI_ID_BT_START_ADV = SPI_CMD(SPI_CAT_BT, 0x59),
  SPI_ID_BT_STOP_ADV = SPI_CMD(SPI_CAT_BT, 0x5A),
  SPI_ID_BT_SET_MAX_POWER = SPI_CMD(SPI_CAT_BT, 0x5B),
  SPI_ID_BT_TRACKER_START = SPI_CMD(SPI_CAT_BT, 0x5C),
  SPI_ID_BT_TRACKER_STOP = SPI_CMD(SPI_CAT_BT, 0x5D),
  SPI_ID_BT_GET_ADDR_TYPE = SPI_CMD(SPI_CAT_BT, 0x5E),
  SPI_ID_BT_SAVE_ANNOUNCE_CFG = SPI_CMD(SPI_CAT_BT, 0x5F),

  // Bluetooth Apps & Attacks
  SPI_ID_BT_APP_SCANNER = SPI_CMD(SPI_CAT_BT, 0x60),
  SPI_ID_BT_APP_SNIFFER = SPI_CMD(SPI_CAT_BT, 0x61),
  SPI_ID_BT_APP_SPAM = SPI_CMD(SPI_CAT_BT, 0x62),
  SPI_ID_BT_APP_FLOOD = SPI_CMD(SPI_CAT_BT, 0x63),
  SPI_ID_BT_APP_SKIMMER = SPI_CMD(SPI_CAT_BT, 0x64),
  SPI_ID_BT_APP_TRACKER = SPI_CMD(SPI_CAT_BT, 0x65),
  SPI_ID_BT_APP_GATT_EXP = SPI_CMD(SPI_CAT_BT, 0x66),
  SPI_ID_BT_SPAM_LIST_LOAD = SPI_CMD(SPI_CAT_BT, 0x68),
  SPI_ID_BT_SPAM_LIST_BEGIN = SPI_CMD(SPI_CAT_BT, 0x69),
  SPI_ID_BT_SPAM_LIST_ITEM = SPI_CMD(SPI_CAT_BT, 0x6A),
  SPI_ID_BT_SPAM_LIST_COMMIT = SPI_CMD(SPI_CAT_BT, 0x6B),
  SPI_ID_BT_SCREEN_INIT = SPI_CMD(SPI_CAT_BT, 0x6C),
  SPI_ID_BT_SCREEN_DEINIT = SPI_CMD(SPI_CAT_BT, 0x6D),
  SPI_ID_BT_SCREEN_IS_ACTIVE = SPI_CMD(SPI_CAT_BT, 0x6E),
  SPI_ID_BT_SCREEN_SEND_PARTIAL = SPI_CMD(SPI_CAT_BT, 0x6F),
  SPI_ID_BT_L2CAP_STATUS = SPI_CMD(SPI_CAT_BT, 0x70),
  SPI_ID_BT_HID_INIT = SPI_CMD(SPI_CAT_BT, 0x71),
  SPI_ID_BT_HID_DEINIT = SPI_CMD(SPI_CAT_BT, 0x72),
  SPI_ID_BT_HID_IS_CONNECTED = SPI_CMD(SPI_CAT_BT, 0x73),
  SPI_ID_BT_HID_SEND_KEY = SPI_CMD(SPI_CAT_BT, 0x74),

  // LoRa. The SX1262 is P4-local; real LoRa runs via the Meshtastic/MeshCore/
  // RNode stacks + the mesh phone bridges below. These two ids are RESERVED for a
  // future raw-LoRa command surface (custom/proprietary protocol) and are not yet
  // implemented on either chip — if built, it must be a P4-local host-link handler
  // driving the sx1262 driver, not a C5 relay.
  SPI_ID_LORA_RX = SPI_CMD(SPI_CAT_LORA, 0x80),
  SPI_ID_LORA_TX = SPI_CMD(SPI_CAT_LORA, 0x81),

  // Meshtastic phone bridge
  SPI_ID_MESH_BLE_INIT = SPI_CMD(SPI_CAT_MESH, 0x90),
  SPI_ID_MESH_BLE_STOP = SPI_CMD(SPI_CAT_MESH, 0x91),
  SPI_ID_MESH_WIFI_INIT = SPI_CMD(SPI_CAT_MESH, 0x92),
  SPI_ID_MESH_WIFI_STOP = SPI_CMD(SPI_CAT_MESH, 0x93),
  SPI_ID_MESH_FROMRADIO_PUSH = SPI_CMD(SPI_CAT_MESH, 0x94),
  SPI_ID_MESH_LOG_PUSH = SPI_CMD(SPI_CAT_MESH, 0x95),
  SPI_ID_MESH_STATUS = SPI_CMD(SPI_CAT_MESH, 0x96),
  SPI_ID_MESH_TORADIO_STREAM = SPI_CMD(SPI_CAT_MESH, 0x97),

  // MeshCore phone bridge
  SPI_ID_MCORE_BLE_INIT = SPI_CMD(SPI_CAT_MCORE, 0x98),
  SPI_ID_MCORE_BLE_STOP = SPI_CMD(SPI_CAT_MCORE, 0x99),
  SPI_ID_MCORE_TX_PUSH = SPI_CMD(SPI_CAT_MCORE, 0x9A),
  SPI_ID_MCORE_RX_STREAM = SPI_CMD(SPI_CAT_MCORE, 0x9B),
  SPI_ID_MCORE_STATUS = SPI_CMD(SPI_CAT_MCORE, 0x9C),

  // Companion host-link BLE relay (C5 owns BLE; transparent byte ferry — all
  // crypto/auth lives on the P4). Mirrors the MeshCore phone-bridge pattern.
  SPI_ID_HOST_BLE_INIT = SPI_CMD(SPI_CAT_HOST, 0xA0), // P4→C5: start GATT + advertise
  SPI_ID_HOST_BLE_STOP = SPI_CMD(SPI_CAT_HOST, 0xA1), // P4→C5: stop GATT
  SPI_ID_HOST_TX = SPI_CMD(SPI_CAT_HOST, 0xA2),       // P4→C5 push: device→app (BLE notify)
  SPI_ID_HOST_RX = SPI_CMD(SPI_CAT_HOST, 0xA3),       // C5→P4 stream: app→device (BLE write)
  SPI_ID_HOST_STATUS = SPI_CMD(SPI_CAT_HOST, 0xA4),   // poll BLE connection state

  // Session lifecycle (long-running operations)
  // Screen sharing (P4-native, over the USB host link — handled locally, never
  // relayed to the C5). START/STOP/KEY are app->device commands; FRAME is a
  // device->app STREAM carrying RGB565 row-strips of the live screen.
  SPI_ID_SCREEN_START = SPI_CMD(SPI_CAT_SCREEN, 0x01),
  SPI_ID_SCREEN_STOP = SPI_CMD(SPI_CAT_SCREEN, 0x02),
  SPI_ID_SCREEN_KEY = SPI_CMD(SPI_CAT_SCREEN, 0x03),
  SPI_ID_SCREEN_FRAME = SPI_CMD(SPI_CAT_SCREEN, 0x04),

  // IR (P4-native: RMT transmit/receive + DC torch). P4-local host-link
  // commands, handled on the P4 and never relayed to the C5; listed here so the
  // app, P4 and C5 share one id space and never collide.
  SPI_ID_IR_TX = SPI_CMD(SPI_CAT_IR, 0x01),        // app→device: send a decoded signal
  SPI_ID_IR_TX_RAW = SPI_CMD(SPI_CAT_IR, 0x02),    // app→device: send raw RMT symbols
  SPI_ID_IR_RX_START = SPI_CMD(SPI_CAT_IR, 0x03),  // app→device: start streaming capture
  SPI_ID_IR_RX_STOP = SPI_CMD(SPI_CAT_IR, 0x04),   // app→device: stop capture
  SPI_ID_IR_RX_FRAME = SPI_CMD(SPI_CAT_IR, 0x05),  // device→app STREAM: one decoded signal
  SPI_ID_IR_TORCH_ON = SPI_CMD(SPI_CAT_IR, 0x06),  // app→device: IR LED DC torch on
  SPI_ID_IR_TORCH_OFF = SPI_CMD(SPI_CAT_IR, 0x07), // app→device: IR LED DC torch off

  // Sub-GHz (P4-native, CC1101). P4-local host-link commands, never relayed.
  SPI_ID_SUBGHZ_RX_START = SPI_CMD(SPI_CAT_SUBGHZ, 0x01),       // [mode u8][preset u8][freq u32]
  SPI_ID_SUBGHZ_RX_FRAME = SPI_CMD(SPI_CAT_SUBGHZ, 0x02),       // device→app STREAM: one capture
  SPI_ID_SUBGHZ_RX_STOP = SPI_CMD(SPI_CAT_SUBGHZ, 0x03),        // stop receiver
  SPI_ID_SUBGHZ_TX_RAW = SPI_CMD(SPI_CAT_SUBGHZ, 0x04),         // [count u16][timings i32 * count]
  SPI_ID_SUBGHZ_REPLAY = SPI_CMD(SPI_CAT_SUBGHZ, 0x05),         // [name] replay a saved capture
  SPI_ID_SUBGHZ_SPECTRUM_START = SPI_CMD(SPI_CAT_SUBGHZ, 0x06), // [center u32][span u32]
  SPI_ID_SUBGHZ_SPECTRUM_LINE = SPI_CMD(SPI_CAT_SUBGHZ, 0x07),  // device→app STREAM: one sweep line
  SPI_ID_SUBGHZ_SPECTRUM_STOP = SPI_CMD(SPI_CAT_SUBGHZ, 0x08),  // stop spectrum sweep
  SPI_ID_SUBGHZ_LIST = SPI_CMD(SPI_CAT_SUBGHZ, 0x09),           // list saved captures via data pipe
  SPI_ID_SUBGHZ_DELETE = SPI_CMD(SPI_CAT_SUBGHZ, 0x0A),         // [name] delete a saved capture

  // Audio (P4-native, speaker). P4-local host-link commands, never relayed.
  SPI_ID_AUDIO_SET_VOLUME = SPI_CMD(SPI_CAT_AUDIO, 0x01), // [pct u8]
  SPI_ID_AUDIO_TONE = SPI_CMD(SPI_CAT_AUDIO, 0x02),       // [freq u16][dur_ms u16][amp_pct u8]
  SPI_ID_AUDIO_CHIME = SPI_CMD(SPI_CAT_AUDIO, 0x03),      // play the UI chime
  SPI_ID_AUDIO_CLICK = SPI_CMD(SPI_CAT_AUDIO, 0x04),      // play the UI click

  // RGB status LED (P4-native). P4-local host-link commands, never relayed.
  SPI_ID_LED_SET_COLOR = SPI_CMD(SPI_CAT_LED, 0x01), // [r u8][g u8][b u8]
  SPI_ID_LED_CLEAR = SPI_CMD(SPI_CAT_LED, 0x02),     // turn the LED off
  SPI_ID_LED_BLINK = SPI_CMD(SPI_CAT_LED, 0x03),     // [r u8][g u8][b u8][dur_ms u16]
  SPI_ID_LED_SIGNAL = SPI_CMD(SPI_CAT_LED, 0x04),    // [which u8]: 0 info, 1 warning, 2 error

  // Companion BadUSB (P4-native; category 0x0C). Handled on the P4, never relayed.
  SPI_ID_BADUSB_RUN = SPI_CMD(SPI_CAT_BADUSB, 0x10),
  SPI_ID_BADUSB_ABORT = SPI_CMD(SPI_CAT_BADUSB, 0x11),
  SPI_ID_BADUSB_STATUS = SPI_CMD(SPI_CAT_BADUSB, 0x12),

  // Companion LoRa config + chat (P4-native; categories 0x0D/0x0E). Handled on
  // the P4, never relayed.
  SPI_ID_LORACFG_GET = SPI_CMD(SPI_CAT_LORACFG, 0x10),
  SPI_ID_LORACFG_SET = SPI_CMD(SPI_CAT_LORACFG, 0x11),
  SPI_ID_LORACHAT_START = SPI_CMD(SPI_CAT_LORACHAT, 0x10),
  SPI_ID_LORACHAT_STOP = SPI_CMD(SPI_CAT_LORACHAT, 0x11),
  SPI_ID_LORACHAT_SEND = SPI_CMD(SPI_CAT_LORACHAT, 0x12),
  SPI_ID_LORACHAT_RX = SPI_CMD(SPI_CAT_LORACHAT, 0x60),

  SPI_ID_SESSION_HEARTBEAT = SPI_CMD(SPI_CAT_SESSION, 0xF0),
  SPI_ID_SESSION_LOST = SPI_CMD(SPI_CAT_SESSION, 0xF1),
  SPI_ID_SESSION_STOP = SPI_CMD(SPI_CAT_SESSION, 0xF2)
} spi_id_t;

/**
 * @brief Config-cache file ids, shared by the P4 (master) and C5 (consumer).
 * Each id maps to a fixed config path on each chip; the P4 owns the master copy
 * and pushes it to the C5 when the hashes differ. Keep this list in sync on both.
 */
typedef enum {
  SPI_CFG_WIFI_AP = 0,   // config/wifi/wifi_ap.conf
  SPI_CFG_BLE_ANNOUNCE,  // config/bluetooth/ble_announce.conf
  SPI_CFG_BLE_SPAM,      // config/bluetooth/beacon_list.conf
  SPI_CFG_CHAT,          // config/chat/chat.conf
  SPI_CFG_CHAT_ADDRS,    // config/chat/addresses.conf
  SPI_CFG_COUNT
} spi_cfg_id_t;

/** @brief SPI_ID_SYSTEM_CFG_BEGIN request payload. */
typedef struct __attribute__((packed)) {
  uint8_t id;    ///< spi_cfg_id_t
  uint32_t size; ///< total config size in bytes
  uint32_t hash; ///< CRC32 of the config bytes (esp_crc32_le)
} spi_cfg_begin_t;

/**
 * @brief Device power state, sent P4→C5 as the SPI_ID_SYSTEM_POWER_STATE payload.
 */
typedef enum {
  SPI_POWER_ACTIVE = 0, ///< Normal operation: full radio.
  SPI_POWER_IDLE = 1,   ///< Screen dimmed: deeper modem sleep (MAX_MODEM).
  SPI_POWER_SLEEP = 2,  ///< Device asleep: drop the radio (unless a capture is running).
} spi_power_state_t;

/**
 * @brief Companion settings section id (payload byte 0 of SPI_ID_SYSTEM_CONFIG_*).
 * Each section maps to one tos_config global and one packed struct below.
 */
typedef enum {
  SPI_CFG_SECTION_DISPLAY = 0,
  SPI_CFG_SECTION_SOUND = 1,
  SPI_CFG_SECTION_CONNECTIVITY = 2,
  SPI_CFG_SECTION_LED = 3,
} spi_config_section_t;

/** @brief Display section (g_config_screen). */
typedef struct __attribute__((packed)) {
  uint8_t brightness;         // 0-100
  uint8_t rotation;           // 1 portrait, 2 landscape
  uint16_t auto_lock_seconds; // 0 = never
  uint8_t auto_dim;           // bool
} spi_cfg_display_t;

/** @brief Sound section (g_config_system). */
typedef struct __attribute__((packed)) {
  uint8_t volume;    // 0-100
  uint8_t vibration; // bool
} spi_cfg_sound_t;

/** @brief Connectivity section (g_config_wifi.enabled + g_config_ble.enabled). */
typedef struct __attribute__((packed)) {
  uint8_t wifi_enabled; // bool
  uint8_t ble_enabled;  // bool
} spi_cfg_connectivity_t;

/** @brief LED section (g_config_led.brightness). */
typedef struct __attribute__((packed)) {
  uint8_t brightness; // 0-100
} spi_cfg_led_t;

/**
 * @brief Screen-share control keys (payload byte 0 of SPI_ID_SCREEN_KEY).
 * The device maps these to the LVGL keypad (up/down/ok/back/left/right).
 */
typedef enum {
  SPI_SCREEN_KEY_UP = 0,
  SPI_SCREEN_KEY_DOWN = 1,
  SPI_SCREEN_KEY_LEFT = 2,
  SPI_SCREEN_KEY_RIGHT = 3,
  SPI_SCREEN_KEY_OK = 4,
  SPI_SCREEN_KEY_BACK = 5
} spi_screen_key_t;

/**
 * @brief Header of a SPI_ID_SCREEN_FRAME STREAM payload, followed by
 * (rows * width) RGB565 little-endian pixels. The screen is streamed as
 * horizontal row-strips; y==0 marks the first strip of a new frame.
 */
typedef struct __attribute__((packed)) {
  uint16_t y;     // row offset of this strip within the frame
  uint16_t rows;  // number of rows in this strip
  uint16_t width; // pixels per row (full screen width)
} spi_screen_strip_t;

/**
 * @brief SPI response status codes (payload byte 0 for RESP type).
 */
typedef enum {
  SPI_STATUS_OK = 0x00,
  SPI_STATUS_BUSY = 0x01,
  SPI_STATUS_ERROR = 0x02,
  SPI_STATUS_UNSUPPORTED = 0x03,
  SPI_STATUS_INVALID_ARG = 0x04
} spi_status_t;

/**
 * @brief App-OTA state, reported by SPI_ID_SYSTEM_OTA_STATUS.
 */
typedef enum {
  SPI_OTA_STATE_IDLE = 0,      ///< No OTA in progress.
  SPI_OTA_STATE_ERASING = 1,   ///< esp_ota_begin erasing the target partition.
  SPI_OTA_STATE_READY = 2,     ///< Erased; ready to receive the .bin over UART0.
  SPI_OTA_STATE_RECEIVING = 3, ///< Receiving/writing the image.
  SPI_OTA_STATE_DONE = 4,      ///< Image validated and set to boot; C5 about to reboot.
  SPI_OTA_STATE_ERROR = 5      ///< Aborted (bad size, timeout, write/verify failure).
} spi_ota_state_t;

/**
 * @brief OTA progress snapshot (SPI_ID_SYSTEM_OTA_STATUS response payload).
 */
typedef struct __attribute__((packed)) {
  uint8_t state;          ///< spi_ota_state_t
  uint32_t bytes_written; ///< Bytes committed to flash so far (the per-block ACK).
} spi_ota_status_t;

/** @brief Where the C5 receives the firmware image after an OTA begin. */
typedef enum {
  SPI_OTA_TRANSPORT_SPI = 0, ///< Image bytes arrive as SPI_ID_SYSTEM_OTA_DATA chunks.
  SPI_OTA_TRANSPORT_UART = 1 ///< Image bytes arrive raw on UART0; control stays on SPI.
} spi_ota_transport_t;

/** @brief SPI_ID_SYSTEM_OTA_BEGIN request payload. */
typedef struct __attribute__((packed)) {
  uint32_t size;     ///< Image size in bytes.
  uint8_t transport; ///< spi_ota_transport_t
} spi_ota_begin_t;

/** @brief SPI_ID_SYSTEM_INFO response payload: C5 chip identity. */
typedef struct __attribute__((packed)) {
  uint8_t chip_model;     ///< esp_chip_model_t
  uint16_t chip_revision; ///< major*100 + minor
  uint8_t mac[6];         ///< base MAC
  uint32_t free_heap;     ///< current free heap in bytes
} spi_sys_info_t;

/**
 * @brief SPI frame header (7 bytes: 5-byte framing + a 2-byte CRC-16).
 *
 * Packed so the layout is identical byte-for-byte on the P4 and C5 (both
 * little-endian). `crc` is validated on every received frame; a mismatch means
 * the bus corrupted the frame and the receiver drops it (see spi_frame_valid).
 */
typedef struct __attribute__((packed)) {
  uint8_t sync;
  uint8_t type;     // spi_type_t
  uint8_t category; // spi_cat_t
  uint8_t op;       // operation within the category
  uint8_t length;   // Payload length
  uint16_t crc;     // CRC-16 over [type,category,op,length] + data
} spi_header_t;

/** Read the packed command identifier (spi_id_t) from a header. */
static inline uint16_t spi_header_cmd(const spi_header_t *h) {
  return SPI_CMD(h->category, h->op);
}

/** Write a packed command identifier (spi_id_t) into a header. */
static inline void spi_header_set_cmd(spi_header_t *h, uint16_t cmd) {
  h->category = SPI_CMD_CAT(cmd);
  h->op = SPI_CMD_OP(cmd);
}

// Frame integrity (CRC-16/CCITT via the ROM implementation). The CRC covers the
// header's [type,category,op,length] plus the `data_len` data bytes that follow
// the header in the frame buffer; the sync byte (framing marker) and the crc
// field itself are excluded. Header and data are always contiguous in a frame
// buffer, so callers pass only the data length: header.length for CMD/RESP
// frames, or (2 + batch_len) for STREAM frames (whose header.length is 0 and
// whose real size lives in the leading u16 of the payload).
static inline uint16_t spi_frame_crc(const spi_header_t *h, uint16_t data_len) {
  uint16_t crc = esp_rom_crc16_le(0xFFFF, &h->type, 4);
  if (data_len > 0) {
    crc = esp_rom_crc16_le(crc, (const uint8_t *)h + sizeof(spi_header_t), data_len);
  }
  return crc;
}

/** Stamp the CRC into a freshly built frame (call after header + data are set). */
static inline void spi_frame_seal(spi_header_t *h, uint16_t data_len) {
  uint16_t c = spi_frame_crc(h, data_len);
  memcpy(&h->crc, &c, sizeof(c)); // memcpy: crc is at an odd (packed) offset
}

/** True if the frame's stored CRC matches a recompute over `data_len` bytes. */
static inline bool spi_frame_valid(const spi_header_t *h, uint16_t data_len) {
  uint16_t stored;
  memcpy(&stored, &h->crc, sizeof(stored));
  return stored == spi_frame_crc(h, data_len);
}

// Session protocol — see spi_bridge/README.md "Session Lifecycle"
#define SPI_SESSION_INVALID_ID 0u
#define SPI_SESSION_WINDOW     64u

/** Sent by C5 as response payload to a long-running START command.
 *  Valid only if the SPI response status byte is SPI_STATUS_OK. */
typedef struct __attribute__((packed)) {
  uint32_t session_id;
} spi_session_resp_t;

/** Sent by P4 every ~2s to keep a session alive and ack streams received. */
typedef struct __attribute__((packed)) {
  uint32_t session_id;
  uint32_t last_acked_seq; // highest stream seq P4 has processed
} spi_heartbeat_req_t;

/** C5 reply to heartbeat. */
typedef struct __attribute__((packed)) {
  uint8_t alive; // 1 if session still active, 0 if not found / different op
} spi_heartbeat_resp_t;

/** Prefixed to every stream payload from a session-managed operation. */
typedef struct __attribute__((packed)) {
  uint32_t session_id;
  uint32_t seq;
} spi_stream_meta_t;

/** Sent by P4 in payload of long-running STOP commands. */
typedef struct __attribute__((packed)) {
  uint32_t session_id;
} spi_session_stop_req_t;

/** Stream emitted by C5 when a session is auto-killed by the watchdog. */
typedef struct __attribute__((packed)) {
  uint32_t session_id;
  uint16_t cmd; // spi_id_t of the lost operation
} spi_session_lost_t;

// Rounded up to a multiple of 4 bytes: SPI DMA transfers must be word-aligned
// in length, and the 7-byte header would otherwise make the frame size odd.
#define SPI_FRAME_SIZE (((sizeof(spi_header_t) + SPI_MAX_PAYLOAD) + 3u) & ~3u)

// Larger fixed transfer size used ONLY by the SYSTEM_STREAM response, which
// batches many stream records into one transfer to amortize per-frame overhead.
// The command/response path keeps using SPI_FRAME_SIZE. Must be a multiple of 4
// for SPI DMA. Stream frame layout (after the 7-byte header, type = STREAM):
//   [u16 batch_len][record]... where each record = [u16 op][u8 len][len bytes]
// and `record` data is exactly what session_manager queued (meta + payload).
#define SPI_STREAM_FRAME_SIZE 2048

/**
 * @brief WiFi connect request payload.
 */
typedef struct {
  char ssid[32];
  char password[64];
} __attribute__((packed)) spi_wifi_connect_t;

/**
 * @brief WiFi AP configuration payload.
 */
typedef struct {
  char ssid[32];
  char password[64];
  uint8_t max_conn;
  char ip_addr[16];
  uint8_t enabled;
} __attribute__((packed)) spi_wifi_ap_config_t;

/**
 * @brief Bluetooth device info response payload.
 */
typedef struct {
  uint8_t mac[6];
  uint8_t running;
  uint8_t initialized;
  uint16_t connected_count;
} __attribute__((packed)) spi_bt_info_t;

/**
 * @brief Bluetooth announce configuration payload.
 */
typedef struct {
  char name[32];
  uint8_t max_conn;
} __attribute__((packed)) spi_bt_announce_config_t;

/**
 * @brief System status response payload.
 */
typedef struct {
  uint8_t wifi_active;
  uint8_t wifi_connected;
  uint8_t bt_running;
  uint8_t bt_initialized;
} __attribute__((packed)) spi_system_status_t;

/**
 * @brief WiFi sniffer statistics payload.
 */
typedef struct {
  uint32_t packets;
  uint32_t deauths;
  uint32_t buffer_usage;
  int8_t signal_rssi;
  bool handshake_captured;
  bool pmkid_captured;
  // Extended monitor stats (appended; older readers can ignore the tail).
  uint32_t beacons;     // mgmt beacon frames
  uint32_t probe_reqs;  // mgmt probe requests
  uint32_t probe_resps; // mgmt probe responses
  uint32_t data_frames; // data frames
  uint32_t ctrl_frames; // control frames
  uint32_t mgmt_frames; // all management frames
  uint32_t pkts_2ghz;   // frames captured on 2.4 GHz
  uint32_t pkts_5ghz;   // frames captured on 5 GHz
  uint32_t unique_aps;  // distinct BSSIDs seen (approximate)
  uint8_t channel;      // channel of the last captured frame
  int8_t last_rssi;     // RSSI of the last captured frame (sniffer, not signal monitor)
} __attribute__((packed)) spi_sniffer_stats_t;

#define SPI_WIFI_SNIFFER_MAX_DATA (SPI_MAX_PAYLOAD - 4)

/**
 * @brief Compact WiFi scan result, served by SPI_ID_WIFI_APP_SCAN_AP through the
 * generic data pipe. Fixed-size and explicit (unlike the raw wifi_ap_record_t)
 * so the companion app parses it without depending on the IDF struct layout.
 * ssid is sanitized printable ASCII, null-terminated, empty for hidden networks.
 */
typedef struct {
  uint8_t bssid[6]; // AP MAC
  int8_t rssi;      // signal strength, dBm
  uint8_t channel;  // primary channel
  uint8_t authmode; // wifi_auth_mode_t value (0=open, 3=wpa2, 6=wpa3, ...)
  uint8_t ssid[33]; // null-terminated, sanitized ASCII ('' = hidden)
} __attribute__((packed)) spi_wifi_scan_record_t;

/**
 * @brief Detailed WiFi scan result, served by SPI_ID_WIFI_APP_SCAN_AP_DETAIL
 * through the same data pipe. Superset of spi_wifi_scan_record_t adding the
 * secondary channel, pairwise/group ciphers, PHY flags and country code so the
 * companion app can show a full AP profile. Fixed-size and explicit.
 */
typedef struct {
  uint8_t bssid[6];        // AP MAC
  int8_t rssi;             // signal strength, dBm
  uint8_t channel;         // primary channel
  uint8_t second;          // wifi_second_chan_t (0 none, 1 above, 2 below)
  uint8_t authmode;        // wifi_auth_mode_t value
  uint8_t pairwise_cipher; // wifi_cipher_type_t
  uint8_t group_cipher;    // wifi_cipher_type_t
  uint8_t phy;             // bit0 11b, 1 11g, 2 11n, 3 11ax, 4 LR, 5 WPS
  uint8_t country[3];      // country code (cc), null-padded
  uint8_t ssid[33];        // null-terminated, sanitized ASCII ('' = hidden)
} __attribute__((packed)) spi_wifi_scan_record_ex_t;

/**
 * @brief WiFi sniffer stream fragment.
 *
 * A single 802.11 frame can exceed one SPI transfer (payload capped at
 * SPI_MAX_PAYLOAD), so the C5 splits large frames into ordered fragments and
 * the P4 reassembles them. rssi/channel/total_len are repeated on every
 * fragment so the P4 can validate cheaply. frag_off is this fragment's byte
 * offset within the full frame; SPI_WIFI_SNIFFER_FRAG_MORE is set on every
 * fragment except the last. A frame that fits in one transfer is a single
 * fragment with frag_off == 0 and the MORE bit clear.
 *
 * Max data bytes per fragment = SPI_MAX_PAYLOAD - sizeof(spi_stream_meta_t)
 * - sizeof(spi_wifi_sniffer_frame_t).
 */
typedef struct {
  int8_t rssi;        // dBm signal of the frame
  uint8_t channel;    // primary channel
  uint16_t total_len; // full 802.11 frame length across all fragments
  uint16_t frag_off;  // byte offset of this fragment within the frame
  uint8_t frag_len;   // data bytes carried by this fragment
  uint8_t flags;      // SPI_WIFI_SNIFFER_FRAG_* bits
  uint8_t data[0];
} __attribute__((packed)) spi_wifi_sniffer_frame_t;

#define SPI_WIFI_SNIFFER_FRAG_MORE 0x01u // more fragments follow this one
#define SPI_WIFI_SNIFFER_FRAME_MAX 2346  // max reassembled 802.11 frame (bytes)

// Max frame bytes carried by a single fragment (transfer cap minus the stream
// meta prepended by the session layer minus this fragment header).
#define SPI_WIFI_SNIFFER_FRAG_DATA_MAX \
  ((int)(SPI_MAX_PAYLOAD - sizeof(spi_stream_meta_t) - sizeof(spi_wifi_sniffer_frame_t)))

/**
 * @brief BLE sniffer stream frame.
 */
typedef struct {
  uint8_t addr[6];
  uint8_t addr_type;
  int8_t rssi;
  uint8_t len;
  uint8_t data[31];
} __attribute__((packed)) spi_ble_sniffer_frame_t;

/**
 * @brief WiFi IP info response payload.
 */
typedef struct {
  uint8_t interface; // 0 = STA, 1 = AP
  uint8_t mac[6];
  uint32_t ip;
  uint32_t netmask;
  uint32_t gw;
} __attribute__((packed)) spi_wifi_ip_info_t;

/**
 * @brief Port scan request with target IP and port range.
 */
typedef struct {
  char ip[16];
  uint16_t start_port;
  uint16_t end_port;
  uint16_t max_results;
  uint8_t reserved[2];
} __attribute__((packed)) spi_port_scan_range_req_t;

/**
 * @brief Port scan request for a network IP range.
 */
typedef struct {
  char start_ip[16];
  char end_ip[16];
  uint16_t start_port;
  uint16_t end_port;
  uint16_t max_results;
  uint8_t scan_type; // 0 = port range, 1 = port list
  uint8_t reserved;
} __attribute__((packed)) spi_port_scan_network_req_t;

/**
 * @brief Port scan request using CIDR notation.
 */
typedef struct {
  char base_ip[16];
  uint8_t cidr;
  uint8_t scan_type; // 0 = port range, 1 = port list
  uint16_t start_port;
  uint16_t end_port;
  uint16_t max_results;
} __attribute__((packed)) spi_port_scan_cidr_req_t;

/**
 * @brief Port scan result record.
 */
typedef struct {
  char ip_str[16];
  uint16_t port;
  uint8_t protocol; // 0 = TCP, 1 = UDP
  uint8_t status;   // 0 = OPEN, 1 = OPEN_FILTERED
  char banner[64];
} __attribute__((packed)) spi_port_scan_result_t;

/**
 * @brief Meshtastic transport init payload.
 *
 * Sent with SPI_ID_MESH_BLE_INIT and SPI_ID_MESH_WIFI_INIT.
 */
typedef struct {
  uint32_t node_num;
} __attribute__((packed)) spi_mesh_init_t;

/**
 * @brief Chunk header for fragmented Meshtastic transport frames.
 *
 * Each StreamAPI protobuf frame is split into 1..255 chunks of up to
 * SPI_MESH_CHUNK_PAYLOAD_MAX bytes. Receivers reassemble per seq.
 */
typedef struct {
  uint8_t seq;
  uint8_t chunk_idx;
  uint8_t total_chunks;
  uint8_t flags;
} __attribute__((packed)) spi_mesh_chunk_hdr_t;

#define SPI_MESH_CHUNK_FLAG_LAST   0x01
#define SPI_MESH_PAYLOAD_LIMIT     255
#define SPI_MESH_CHUNK_PAYLOAD_MAX (SPI_MESH_PAYLOAD_LIMIT - sizeof(spi_mesh_chunk_hdr_t))

/**
 * @brief Meshtastic transport status payload.
 *
 * Returned by SPI_ID_MESH_STATUS.
 */
typedef struct {
  uint8_t ble_connected;
  uint8_t ble_subscribed;
  uint8_t tcp_clients;
  uint8_t logradio_subscribed;
  uint32_t fromnum_counter;
} __attribute__((packed)) spi_mesh_status_t;

/**
 * @brief MeshCore BLE init payload.
 *
 * Sent with SPI_ID_MCORE_BLE_INIT. The C5 builds the advertised name as
 * "<name_prefix>-XXXX" (last 4 hex of MAC) and uses `pin` as the static
 * passkey for SC + MITM + DISP_ONLY pairing.
 */
typedef struct {
  char name_prefix[16];
  uint32_t pin;
} __attribute__((packed)) spi_mcore_init_t;

/**
 * @brief MeshCore transport status payload.
 *
 * Returned by SPI_ID_MCORE_STATUS.
 */
typedef struct {
  uint8_t ble_connected;
  uint8_t ble_subscribed;
  uint8_t reserved[2];
} __attribute__((packed)) spi_mcore_status_t;

/**
 * @brief Companion host-link BLE init payload.
 *
 * Sent with SPI_ID_HOST_BLE_INIT. The C5 advertises as "<name_prefix>-XXXX"
 * (last 4 hex of MAC). BLE bonding is "just works" (LE Secure Connections,
 * no MITM) — the host-link PSK/HMAC envelope on the P4 is the trust boundary.
 */
typedef struct {
  char name_prefix[16];
} __attribute__((packed)) spi_host_init_t;

/**
 * @brief Companion host-link transport status payload.
 *
 * Returned by SPI_ID_HOST_STATUS.
 */
typedef struct {
  uint8_t ble_connected;
  uint8_t ble_subscribed;
  uint8_t reserved[2];
} __attribute__((packed)) spi_host_status_t;

#ifdef __cplusplus
}
#endif

#endif // SPI_PROTOCOL_H
