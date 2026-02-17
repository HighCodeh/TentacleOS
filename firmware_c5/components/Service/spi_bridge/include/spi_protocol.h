#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#define SPI_SYNC_BYTE 0xAA
#define SPI_MAX_PAYLOAD 256
#define SPI_RESP_STATUS_SIZE 1
#define SPI_BT_SPAM_ITEM_LEN 32
#define SPI_BT_SPAM_LIST_MAX 64
#define SPI_WIFI_SNIFFER_FILENAME_MAX 96

// Special indices for SPI_ID_SYSTEM_DATA
#define SPI_DATA_INDEX_COUNT 0xFFFF
#define SPI_DATA_INDEX_STATS 0xEEEE
#define SPI_DATA_INDEX_DEAUTH_COUNT 0xDDDD

// Message Types
typedef enum {
    SPI_TYPE_CMD    = 0x01,
    SPI_TYPE_RESP   = 0x02,
    SPI_TYPE_STREAM = 0x03
} spi_type_t;

// Function IDs
typedef enum {
    // System (0x01 - 0x0F)
    SPI_ID_SYSTEM_PING          = 0x01,
    SPI_ID_SYSTEM_STATUS        = 0x02,
    SPI_ID_SYSTEM_REBOOT        = 0x03,
    SPI_ID_SYSTEM_VERSION       = 0x04,
    SPI_ID_SYSTEM_DATA          = 0x05,
    SPI_ID_SYSTEM_STREAM        = 0x06,

    // WiFi Basic (0x10 - 0x1F)
    SPI_ID_WIFI_SCAN            = 0x10,
    SPI_ID_WIFI_CONNECT         = 0x11,
    SPI_ID_WIFI_DISCONNECT      = 0x12,
    SPI_ID_WIFI_GET_STA_INFO    = 0x13,
    SPI_ID_WIFI_SET_AP          = 0x14, // SSID only (legacy)
    SPI_ID_WIFI_START           = 0x15,
    SPI_ID_WIFI_STOP            = 0x16,
    SPI_ID_WIFI_SAVE_AP_CONFIG  = 0x17,
    SPI_ID_WIFI_SET_ENABLED     = 0x18,
    SPI_ID_WIFI_SET_AP_PASSWORD = 0x19,
    SPI_ID_WIFI_SET_AP_MAX_CONN = 0x1A,
    SPI_ID_WIFI_SET_AP_IP       = 0x1B,
    SPI_ID_WIFI_PROMISC_START   = 0x1C,
    SPI_ID_WIFI_PROMISC_STOP    = 0x1D,
    SPI_ID_WIFI_CH_HOP_START    = 0x1E,
    SPI_ID_WIFI_CH_HOP_STOP     = 0x1F,

    // WiFi Applications & Attacks (0x20 - 0x3F)
    SPI_ID_WIFI_APP_SCAN_AP     = 0x20,
    SPI_ID_WIFI_APP_SCAN_CLIENT = 0x21,
    SPI_ID_WIFI_APP_BEACON_SPAM = 0x22,
    SPI_ID_WIFI_APP_DEAUTHER    = 0x23,
    SPI_ID_WIFI_APP_FLOOD       = 0x24,
    SPI_ID_WIFI_APP_SNIFFER     = 0x25,
    SPI_ID_WIFI_APP_EVIL_TWIN   = 0x26,
    SPI_ID_WIFI_APP_DEAUTH_DET  = 0x27,
    SPI_ID_WIFI_APP_PROBE_MON   = 0x28,
    SPI_ID_WIFI_APP_SIGNAL_MON  = 0x29,
    SPI_ID_WIFI_APP_ATTACK_STOP = 0x2A,
    SPI_ID_WIFI_SNIFFER_SET_SNAPLEN        = 0x2B,
    SPI_ID_WIFI_SNIFFER_SET_VERBOSE        = 0x2C,
    SPI_ID_WIFI_SNIFFER_SAVE_FLASH         = 0x2D,
    SPI_ID_WIFI_SNIFFER_SAVE_SD            = 0x2E,
    SPI_ID_WIFI_SNIFFER_FREE_BUFFER        = 0x2F,
    SPI_ID_WIFI_SNIFFER_STREAM_SD          = 0x30,
    SPI_ID_WIFI_SNIFFER_CLEAR_PMKID        = 0x31,
    SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID    = 0x32,
    SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE    = 0x33,
    SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID= 0x34,
    SPI_ID_WIFI_DEAUTH_STATUS             = 0x35,
    SPI_ID_WIFI_DEAUTH_SEND_RAW            = 0x36,
    SPI_ID_WIFI_ASSOC_REQUEST              = 0x37,
    SPI_ID_WIFI_DEAUTH_SEND_FRAME          = 0x38,
    SPI_ID_WIFI_DEAUTH_SEND_BROADCAST      = 0x39,
    SPI_ID_WIFI_TARGET_SCAN_START          = 0x3A,
    SPI_ID_WIFI_TARGET_SCAN_STATUS         = 0x3B,
    SPI_ID_WIFI_TARGET_SAVE_FLASH          = 0x3C,
    SPI_ID_WIFI_TARGET_SAVE_SD             = 0x3D,
    SPI_ID_WIFI_TARGET_FREE                = 0x3E,
    SPI_ID_WIFI_PROBE_SAVE_FLASH           = 0x3F,
    SPI_ID_WIFI_PROBE_SAVE_SD              = 0x40,
    SPI_ID_WIFI_EVIL_TWIN_TEMPLATE         = 0x41,
    SPI_ID_WIFI_EVIL_TWIN_HAS_PASSWORD     = 0x42,
    SPI_ID_WIFI_EVIL_TWIN_GET_PASSWORD     = 0x43,
    SPI_ID_WIFI_EVIL_TWIN_RESET_CAPTURE    = 0x44,
    SPI_ID_WIFI_CLIENT_SAVE_FLASH          = 0x45,
    SPI_ID_WIFI_CLIENT_SAVE_SD             = 0x46,
    SPI_ID_WIFI_AP_SAVE_FLASH              = 0x47,
    SPI_ID_WIFI_AP_SAVE_SD                 = 0x48,

    // Bluetooth Basic (0x50 - 0x5F)
    SPI_ID_BT_SCAN              = 0x50,
    SPI_ID_BT_CONNECT           = 0x51,
    SPI_ID_BT_DISCONNECT        = 0x52,
    SPI_ID_BT_GET_INFO          = 0x53,
    SPI_ID_BT_INIT              = 0x54,
    SPI_ID_BT_DEINIT            = 0x55,
    SPI_ID_BT_START             = 0x56,
    SPI_ID_BT_STOP              = 0x57,
    SPI_ID_BT_SET_RANDOM_MAC    = 0x58,
    SPI_ID_BT_START_ADV         = 0x59,
    SPI_ID_BT_STOP_ADV          = 0x5A,
    SPI_ID_BT_SET_MAX_POWER     = 0x5B,
    SPI_ID_BT_TRACKER_START     = 0x5C,
    SPI_ID_BT_TRACKER_STOP      = 0x5D,
    SPI_ID_BT_GET_ADDR_TYPE     = 0x5E,
    SPI_ID_BT_SAVE_ANNOUNCE_CFG = 0x5F,

    // Bluetooth Apps & Attacks (0x60 - 0x7F)
    SPI_ID_BT_APP_SCANNER       = 0x60,
    SPI_ID_BT_APP_SNIFFER       = 0x61,
    SPI_ID_BT_APP_SPAM          = 0x62,
    SPI_ID_BT_APP_FLOOD         = 0x63,
    SPI_ID_BT_APP_SKIMMER       = 0x64,
    SPI_ID_BT_APP_TRACKER       = 0x65,
    SPI_ID_BT_APP_GATT_EXP      = 0x66,
    SPI_ID_BT_APP_STOP          = 0x67,
    SPI_ID_BT_SPAM_LIST_LOAD    = 0x68,
    SPI_ID_BT_SPAM_LIST_BEGIN   = 0x69,
    SPI_ID_BT_SPAM_LIST_ITEM    = 0x6A,
    SPI_ID_BT_SPAM_LIST_COMMIT  = 0x6B,
    SPI_ID_BT_SCREEN_INIT       = 0x6C,
    SPI_ID_BT_SCREEN_DEINIT     = 0x6D,
    SPI_ID_BT_SCREEN_IS_ACTIVE  = 0x6E,
    SPI_ID_BT_SCREEN_SEND_PARTIAL = 0x6F,
    SPI_ID_BT_L2CAP_STATUS      = 0x70,
    SPI_ID_BT_HID_INIT          = 0x71,
    SPI_ID_BT_HID_DEINIT        = 0x72,
    SPI_ID_BT_HID_IS_CONNECTED  = 0x73,
    SPI_ID_BT_HID_SEND_KEY      = 0x74,

    // LoRa (0x80 - 0x8F)
    SPI_ID_LORA_RX              = 0x80,
    SPI_ID_LORA_TX              = 0x81
} spi_id_t;

// Status Codes (Payload[0] for RESP types)
typedef enum {
    SPI_STATUS_OK          = 0x00,
    SPI_STATUS_BUSY        = 0x01,
    SPI_STATUS_ERROR       = 0x02,
    SPI_STATUS_UNSUPPORTED = 0x03,
    SPI_STATUS_INVALID_ARG = 0x04
} spi_status_t;

// Header Structure (4 bytes)
typedef struct {
    uint8_t sync;
    uint8_t type;   // spi_type_t
    uint8_t id;     // spi_id_t
    uint8_t length; // Payload length
} spi_header_t;

#define SPI_FRAME_SIZE (sizeof(spi_header_t) + SPI_MAX_PAYLOAD)

typedef struct {
    char ssid[32];
    char password[64];
} __attribute__((packed)) spi_wifi_connect_t;

typedef struct {
    char ssid[32];
    char password[64];
    uint8_t max_conn;
    char ip_addr[16];
    uint8_t enabled;
} __attribute__((packed)) spi_wifi_ap_config_t;

typedef struct {
    uint8_t mac[6];
    uint8_t running;
    uint8_t initialized;
    uint16_t connected_count;
} __attribute__((packed)) spi_bt_info_t;

typedef struct {
    char name[32];
    uint8_t max_conn;
} __attribute__((packed)) spi_bt_announce_config_t;

typedef struct {
    uint8_t wifi_active;
    uint8_t wifi_connected;
    uint8_t bt_running;
    uint8_t bt_initialized;
} __attribute__((packed)) spi_system_status_t;

typedef struct {
    uint32_t packets;
    uint32_t deauths;
    uint32_t buffer_usage;
    int8_t signal_rssi;
    bool handshake_captured;
    bool pmkid_captured;
} __attribute__((packed)) sniffer_stats_t;

#define SPI_WIFI_SNIFFER_MAX_DATA (SPI_MAX_PAYLOAD - 4)
typedef struct {
    int8_t rssi;
    uint8_t channel;
    uint8_t len;
    uint8_t data[0];
} __attribute__((packed)) spi_wifi_sniffer_frame_t;

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int8_t rssi;
    uint8_t len;
    uint8_t data[31];
} __attribute__((packed)) spi_ble_sniffer_frame_t;

#endif // SPI_PROTOCOL_H
