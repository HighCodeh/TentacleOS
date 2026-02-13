#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H


#include <stdbool.h>


#include <stdint.h>

#define SPI_SYNC_BYTE 0xAA

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

    // WiFi Basic (0x10 - 0x1F)
    SPI_ID_WIFI_SCAN            = 0x10,
    SPI_ID_WIFI_CONNECT         = 0x11,
    SPI_ID_WIFI_DISCONNECT      = 0x12,
    SPI_ID_WIFI_GET_STA_INFO    = 0x13,
    SPI_ID_WIFI_SET_AP          = 0x14,

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

    // Bluetooth Basic (0x50 - 0x5F)
    SPI_ID_BT_SCAN              = 0x50,
    SPI_ID_BT_CONNECT           = 0x51,
    SPI_ID_BT_DISCONNECT        = 0x52,
    SPI_ID_BT_GET_INFO          = 0x53,

    // Bluetooth Apps & Attacks (0x60 - 0x7F)
    SPI_ID_BT_APP_SCANNER       = 0x60,
    SPI_ID_BT_APP_SNIFFER       = 0x61,
    SPI_ID_BT_APP_SPAM          = 0x62,
    SPI_ID_BT_APP_FLOOD         = 0x63,
    SPI_ID_BT_APP_SKIMMER       = 0x64,
    SPI_ID_BT_APP_TRACKER       = 0x65,
    SPI_ID_BT_APP_GATT_EXP      = 0x66,
    SPI_ID_BT_APP_STOP          = 0x67,

    // LoRa (0x80 - 0x8F)
    SPI_ID_LORA_RX              = 0x80,
    SPI_ID_LORA_TX              = 0x81
} spi_id_t;

// Status Codes (Payload[0] for RESP types)
typedef enum {
    SPI_STATUS_OK    = 0x00,
    SPI_STATUS_BUSY  = 0x01,
    SPI_STATUS_ERROR = 0x02
} spi_status_t;

// Header Structure (4 bytes)
typedef struct {
    uint8_t sync;
    uint8_t type;   // spi_type_t
    uint8_t id;     // spi_id_t
    uint8_t length; // Payload length
} spi_header_t;

typedef struct {
    uint32_t packets;
    uint32_t deauths;
    uint32_t buffer_usage;
    int8_t signal_rssi;
    bool handshake_captured;
    bool pmkid_captured;
} __attribute__((packed)) sniffer_stats_t;

#endif // SPI_PROTOCOL_H
