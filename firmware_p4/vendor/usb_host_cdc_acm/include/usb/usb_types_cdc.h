/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <inttypes.h>

/**
 * @brief USB CDC Descriptor Subtypes
 *
 * @see Table 13, USB CDC specification rev. 1.2
 */
typedef enum {
    USB_CDC_DESC_SUBTYPE_HEADER = 0x00,             /*!< Header functional descriptor. */
    USB_CDC_DESC_SUBTYPE_CALL = 0x01,               /*!< Call management functional descriptor. */
    USB_CDC_DESC_SUBTYPE_ACM = 0x02,                /*!< Abstract control management functional descriptor. */
    USB_CDC_DESC_SUBTYPE_DLM = 0x03,                /*!< Direct line management functional descriptor. */
    USB_CDC_DESC_SUBTYPE_TEL_RINGER = 0x04,         /*!< Telephone ringer functional descriptor. */
    USB_CDC_DESC_SUBTYPE_TEL_CLSR = 0x05,           /*!< Telephone call and line state reporting descriptor. */
    USB_CDC_DESC_SUBTYPE_UNION = 0x06,              /*!< Union functional descriptor. */
    USB_CDC_DESC_SUBTYPE_COUNTRY = 0x07,            /*!< Country selection functional descriptor. */
    USB_CDC_DESC_SUBTYPE_TEL_MODE = 0x08,           /*!< Telephone operational modes descriptor. */
    USB_CDC_DESC_SUBTYPE_TERMINAL = 0x09,           /*!< USB terminal descriptor. */
    USB_CDC_DESC_SUBTYPE_NCHT = 0x0A,               /*!< Network channel terminal descriptor. */
    USB_CDC_DESC_SUBTYPE_PROTOCOL = 0x0B,           /*!< Protocol unit descriptor. */
    USB_CDC_DESC_SUBTYPE_EXTENSION = 0x0C,          /*!< Extension unit descriptor. */
    USB_CDC_DESC_SUBTYPE_MULTI_CHAN = 0x0D,         /*!< Multi-channel management functional descriptor. */
    USB_CDC_DESC_SUBTYPE_CAPI = 0x0E,               /*!< CAPI control descriptor. */
    USB_CDC_DESC_SUBTYPE_ETH = 0x0F,                /*!< Ethernet networking descriptor. */
    USB_CDC_DESC_SUBTYPE_ATM = 0x10,                /*!< ATM networking descriptor. */
    USB_CDC_DESC_SUBTYPE_WHANDSET = 0x11,           /*!< Wireless handset control model descriptor. */
    USB_CDC_DESC_SUBTYPE_MDLM = 0x12,               /*!< Mobile direct line model descriptor. */
    USB_CDC_DESC_SUBTYPE_MDLM_DETAIL = 0x13,        /*!< MDLM detail descriptor. */
    USB_CDC_DESC_SUBTYPE_DMM = 0x14,                /*!< Device management model descriptor. */
    USB_CDC_DESC_SUBTYPE_OBEX = 0x15,               /*!< OBEX functional descriptor. */
    USB_CDC_DESC_SUBTYPE_COMMAND_SET = 0x16,        /*!< Command set descriptor. */
    USB_CDC_DESC_SUBTYPE_COMMAND_SET_DETAIL = 0x17, /*!< Command set detail functional descriptor. */
    USB_CDC_DESC_SUBTYPE_TEL_CM = 0x18,             /*!< Telephone control model functional descriptor. */
    USB_CDC_DESC_SUBTYPE_OBEX_SERVICE = 0x19,       /*!< OBEX service identifier functional descriptor. */
    USB_CDC_DESC_SUBTYPE_NCM = 0x1A,                /*!< NCM functional descriptor. */
    USB_CDC_DESC_SUBTYPE_MAX                         /*!< Sentinel value, not a descriptor subtype. */
} __attribute__((packed)) cdc_desc_subtype_t;

/**
 * @brief USB CDC Subclass codes
 *
 * @see Table 4, USB CDC specification rev. 1.2
 */
typedef enum {
    USB_CDC_SUBCLASS_DLCM = 0x01,    /*!< Direct line control model. */
    USB_CDC_SUBCLASS_ACM = 0x02,     /*!< Abstract control model. */
    USB_CDC_SUBCLASS_TCM = 0x03,     /*!< Telephone control model. */
    USB_CDC_SUBCLASS_MCHCM = 0x04,   /*!< Multi-channel control model. */
    USB_CDC_SUBCLASS_CAPI = 0x05,    /*!< CAPI control model. */
    USB_CDC_SUBCLASS_ECM = 0x06,     /*!< Ethernet networking control model. */
    USB_CDC_SUBCLASS_ATM = 0x07,     /*!< ATM networking model. */
    USB_CDC_SUBCLASS_HANDSET = 0x08, /*!< Wireless handset control model. */
    USB_CDC_SUBCLASS_DEV_MAN = 0x09, /*!< Device management model. */
    USB_CDC_SUBCLASS_MOBILE = 0x0A,  /*!< Mobile direct line model. */
    USB_CDC_SUBCLASS_OBEX = 0x0B,    /*!< OBEX model. */
    USB_CDC_SUBCLASS_EEM = 0x0C,     /*!< Ethernet emulation model. */
    USB_CDC_SUBCLASS_NCM = 0x0D      /*!< Network control model. */
} __attribute__((packed)) cdc_subclass_t;

/**
 * @brief USB CDC Communications Protocol Codes
 *
 * @see Table 5, USB CDC specification rev. 1.2
 */
typedef enum {
    USB_CDC_COMM_PROTOCOL_NONE = 0x00,   /*!< No class-specific protocol required. */
    USB_CDC_COMM_PROTOCOL_V250 = 0x01,   /*!< AT commands defined by V.250 and related specs. */
    USB_CDC_COMM_PROTOCOL_PCAA = 0x02,   /*!< AT commands defined by PCCA-101. */
    USB_CDC_COMM_PROTOCOL_PCAA_A = 0x03, /*!< AT commands defined by PCCA-101 and Annex O. */
    USB_CDC_COMM_PROTOCOL_GSM = 0x04,    /*!< AT commands defined by GSM 07.07. */
    USB_CDC_COMM_PROTOCOL_3GPP = 0x05,   /*!< AT commands defined by 3GPP 27.007. */
    USB_CDC_COMM_PROTOCOL_TIA = 0x06,    /*!< AT commands defined by TIA for CDMA. */
    USB_CDC_COMM_PROTOCOL_EEM = 0x07,    /*!< Ethernet emulation model protocol. */
    USB_CDC_COMM_PROTOCOL_EXT = 0xFE,    /*!< External protocol defined by a command set descriptor. */
    USB_CDC_COMM_PROTOCOL_VENDOR = 0xFF  /*!< Vendor-specific protocol. */
} __attribute__((packed)) cdc_comm_protocol_t;

/**
 * @brief USB CDC Data Protocol Codes
 *
 * @see Table 7, USB CDC specification rev. 1.2
 */
typedef enum {
    USB_CDC_DATA_PROTOCOL_NONE = 0x00,   /*!< No class-specific protocol required. */
    USB_CDC_DATA_PROTOCOL_NCM = 0x01,    /*!< Network transfer block protocol. */
    USB_CDC_DATA_PROTOCOL_I430 = 0x30,   /*!< Physical interface protocol for ISDN BRI. */
    USB_CDC_DATA_PROTOCOL_HDLC = 0x31,   /*!< HDLC protocol. */
    USB_CDC_DATA_PROTOCOL_Q921M = 0x50,  /*!< Management protocol for Q.921 data link protocol. */
    USB_CDC_DATA_PROTOCOL_Q921 = 0x51,   /*!< Data link protocol for Q.931. */
    USB_CDC_DATA_PROTOCOL_Q921TM = 0x52, /*!< TEI multiplexor for Q.921 data link protocol. */
    USB_CDC_DATA_PROTOCOL_V42BIS = 0x90, /*!< Data compression procedures. */
    USB_CDC_DATA_PROTOCOL_Q931 = 0x91,   /*!< Euro-ISDN protocol control. */
    USB_CDC_DATA_PROTOCOL_V120 = 0x92,   /*!< V.24 rate adaptation to ISDN. */
    USB_CDC_DATA_PROTOCOL_CAPI = 0x93,   /*!< CAPI commands protocol. */
    USB_CDC_DATA_PROTOCOL_VENDOR = 0xFF  /*!< Vendor-specific protocol. */
} __attribute__((packed)) cdc_data_protocol_t;

/**
 * @brief USB CDC Request Codes
 *
 * @see Table 19, USB CDC specification rev. 1.2
 */
typedef enum {
    USB_CDC_REQ_SEND_ENCAPSULATED_COMMAND = 0x00, /*!< SEND_ENCAPSULATED_COMMAND request. */
    USB_CDC_REQ_GET_ENCAPSULATED_RESPONSE = 0x01, /*!< GET_ENCAPSULATED_RESPONSE request. */
    USB_CDC_REQ_SET_COMM_FEATURE = 0x02, /*!< SET_COMM_FEATURE request. */
    USB_CDC_REQ_GET_COMM_FEATURE = 0x03, /*!< GET_COMM_FEATURE request. */
    USB_CDC_REQ_CLEAR_COMM_FEATURE = 0x04, /*!< CLEAR_COMM_FEATURE request. */
    USB_CDC_REQ_SET_AUX_LINE_STATE = 0x10, /*!< SET_AUX_LINE_STATE request. */
    USB_CDC_REQ_SET_HOOK_STATE = 0x11, /*!< SET_HOOK_STATE request. */
    USB_CDC_REQ_PULSE_SETUP = 0x12, /*!< PULSE_SETUP request. */
    USB_CDC_REQ_SEND_PULSE = 0x13, /*!< SEND_PULSE request. */
    USB_CDC_REQ_SET_PULSE_TIME = 0x14, /*!< SET_PULSE_TIME request. */
    USB_CDC_REQ_RING_AUX_JACK = 0x15, /*!< RING_AUX_JACK request. */
    USB_CDC_REQ_SET_LINE_CODING = 0x20, /*!< SET_LINE_CODING request. */
    USB_CDC_REQ_GET_LINE_CODING = 0x21, /*!< GET_LINE_CODING request. */
    USB_CDC_REQ_SET_CONTROL_LINE_STATE = 0x22, /*!< SET_CONTROL_LINE_STATE request. */
    USB_CDC_REQ_SEND_BREAK = 0x23, /*!< SEND_BREAK request. */
    USB_CDC_REQ_SET_RINGER_PARMS = 0x30, /*!< SET_RINGER_PARMS request. */
    USB_CDC_REQ_GET_RINGER_PARMS = 0x31, /*!< GET_RINGER_PARMS request. */
    USB_CDC_REQ_SET_OPERATION_PARMS = 0x32, /*!< SET_OPERATION_PARMS request. */
    USB_CDC_REQ_GET_OPERATION_PARMS = 0x33, /*!< GET_OPERATION_PARMS request. */
    USB_CDC_REQ_SET_LINE_PARMS = 0x34, /*!< SET_LINE_PARMS request. */
    USB_CDC_REQ_GET_LINE_PARMS = 0x35, /*!< GET_LINE_PARMS request. */
    USB_CDC_REQ_DIAL_DIGITS = 0x36, /*!< DIAL_DIGITS request. */
    USB_CDC_REQ_SET_UNIT_PARAMETER = 0x37, /*!< SET_UNIT_PARAMETER request. */
    USB_CDC_REQ_GET_UNIT_PARAMETER = 0x38, /*!< GET_UNIT_PARAMETER request. */
    USB_CDC_REQ_CLEAR_UNIT_PARAMETER = 0x39, /*!< CLEAR_UNIT_PARAMETER request. */
    USB_CDC_REQ_GET_PROFILE = 0x3A, /*!< GET_PROFILE request. */
    USB_CDC_REQ_SET_ETHERNET_MULTICAST_FILTERS = 0x40, /*!< SET_ETHERNET_MULTICAST_FILTERS request. */
    USB_CDC_REQ_SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER = 0x41, /*!< Set Ethernet PM pattern filter request. */
    USB_CDC_REQ_GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER = 0x42, /*!< Get Ethernet PM pattern filter request. */
    USB_CDC_REQ_SET_ETHERNET_PACKET_FILTER = 0x43, /*!< SET_ETHERNET_PACKET_FILTER request. */
    USB_CDC_REQ_GET_ETHERNET_STATISTIC = 0x44, /*!< GET_ETHERNET_STATISTIC request. */
    USB_CDC_REQ_SET_ATM_DATA_FORMAT = 0x50, /*!< SET_ATM_DATA_FORMAT request. */
    USB_CDC_REQ_GET_ATM_DEVICE_STATISTICS = 0x51, /*!< GET_ATM_DEVICE_STATISTICS request. */
    USB_CDC_REQ_SET_ATM_DEFAULT_VC = 0x52, /*!< SET_ATM_DEFAULT_VC request. */
    USB_CDC_REQ_GET_ATM_VC_STATISTICS = 0x53, /*!< GET_ATM_VC_STATISTICS request. */
    USB_CDC_REQ_GET_NTB_PARAMETERS = 0x80, /*!< GET_NTB_PARAMETERS request. */
    USB_CDC_REQ_GET_NET_ADDRESS = 0x81, /*!< GET_NET_ADDRESS request. */
    USB_CDC_REQ_SET_NET_ADDRESS = 0x82, /*!< SET_NET_ADDRESS request. */
    USB_CDC_REQ_GET_NTB_FORMAT = 0x83, /*!< GET_NTB_FORMAT request. */
    USB_CDC_REQ_SET_NTB_FORMAT = 0x84, /*!< SET_NTB_FORMAT request. */
    USB_CDC_REQ_GET_NTB_INPUT_SIZE = 0x85, /*!< GET_NTB_INPUT_SIZE request. */
    USB_CDC_REQ_SET_NTB_INPUT_SIZE = 0x86, /*!< SET_NTB_INPUT_SIZE request. */
    USB_CDC_REQ_GET_MAX_DATAGRAM_SIZE = 0x87, /*!< GET_MAX_DATAGRAM_SIZE request. */
    USB_CDC_REQ_SET_MAX_DATAGRAM_SIZE = 0x88, /*!< SET_MAX_DATAGRAM_SIZE request. */
    USB_CDC_REQ_GET_CRC_MODE = 0x89, /*!< GET_CRC_MODE request. */
    USB_CDC_REQ_SET_CRC_MODE = 0x8A /*!< SET_CRC_MODE request. */
} __attribute__((packed)) cdc_request_code_t;

/**
 * @brief USB CDC Notification Codes
 *
 * @see Table 20, USB CDC specification rev. 1.2
 */
typedef enum {
    USB_CDC_NOTIF_NETWORK_CONNECTION = 0x00,     /*!< NETWORK_CONNECTION notification. */
    USB_CDC_NOTIF_RESPONSE_AVAILABLE = 0x01,     /*!< RESPONSE_AVAILABLE notification. */
    USB_CDC_NOTIF_AUX_JACK_HOOK_STATE = 0x08,    /*!< AUX_JACK_HOOK_STATE notification. */
    USB_CDC_NOTIF_RING_DETECT = 0x09,            /*!< RING_DETECT notification. */
    USB_CDC_NOTIF_SERIAL_STATE = 0x20,           /*!< SERIAL_STATE notification. */
    USB_CDC_NOTIF_CALL_STATE_CHANGE = 0x28,      /*!< CALL_STATE_CHANGE notification. */
    USB_CDC_NOTIF_LINE_STATE_CHANGE = 0x29,      /*!< LINE_STATE_CHANGE notification. */
    USB_CDC_NOTIF_CONNECTION_SPEED_CHANGE = 0x2A /*!< CONNECTION_SPEED_CHANGE notification. */
} __attribute__((packed)) cdc_notification_code_t;

/**
 * @brief USB CDC notification packet.
 */
typedef struct {
    uint8_t bmRequestType;                      /*!< `bmRequestType` field. */
    cdc_notification_code_t bNotificationCode; /*!< Notification code. */
    uint16_t wValue;                           /*!< `wValue` field. */
    uint16_t wIndex;                           /*!< `wIndex` field. */
    uint16_t wLength;                          /*!< Notification payload length in bytes. */
    uint8_t Data[];                            /*!< Notification payload. */
} __attribute__((packed)) cdc_notification_t;

/**
 * @brief USB CDC Header Functional Descriptor
 *
 * @see Table 15, USB CDC specification rev. 1.2
 */
typedef struct {
    uint8_t bFunctionLength;                      /*!< Descriptor length in bytes. */
    const uint8_t bDescriptorType;               /*!< Descriptor type. Upper nibble is the CDC code 0x02, lower nibble
                                                      is the interface or endpoint descriptor type 0x04 or 0x05. */
    const cdc_desc_subtype_t bDescriptorSubtype; /*!< Descriptor subtype. */
    uint16_t bcdCDC;                             /*!< CDC version encoded as BCD. This driver targets version 1.2. */
} __attribute__((packed)) cdc_header_desc_t;

/**
 * @brief USB CDC Union Functional Descriptor
 *
 * @see Table 16, USB CDC specification rev. 1.2
 */
typedef struct {
    uint8_t bFunctionLength;                      /*!< Descriptor length in bytes. */
    const uint8_t bDescriptorType;               /*!< Descriptor type. Upper nibble is the CDC code 0x02, lower nibble
                                                      is the interface or endpoint descriptor type 0x04 or 0x05. */
    const cdc_desc_subtype_t bDescriptorSubtype; /*!< Descriptor subtype. */
    const uint8_t bControlInterface;             /*!< Master or controlling interface number. */
    uint8_t bSubordinateInterface[];             /*!< Slave or subordinate interface numbers. */
} __attribute__((packed)) cdc_union_desc_t;

/**
 * @brief USB CDC PSTN Call Descriptor
 *
 * @see Table 3, USB CDC-PSTN specification rev. 1.2
 */
typedef struct {
    uint8_t bFunctionLength;                      /*!< Descriptor length in bytes. */
    const uint8_t bDescriptorType;               /*!< Descriptor type. */
    const cdc_desc_subtype_t bDescriptorSubtype; /*!< Descriptor subtype. */
    union {
        struct {
            uint8_t call_management:   1; /*!< Device handles call management itself. */
            uint8_t call_over_data_if: 1; /*!< Device handles call management over the data class interface. */
            uint8_t reserved: 6;          /*!< Reserved bits. */
        };
        uint8_t val;                          /*!< Raw capabilities bitmap. */
    } bmCapabilities;                        /*!< Call management capabilities. */
    uint8_t bDataInterface;                  /*!< Data class interface optionally used for call management. */
} __attribute__((packed)) cdc_acm_call_desc_t;

/**
 * @brief USB CDC PSTN Abstract Control Model Descriptor
 *
 * @see Table 4, USB CDC-PSTN specification rev. 1.2
 */
typedef struct {
    uint8_t bFunctionLength;                      /*!< Descriptor length in bytes. */
    const uint8_t bDescriptorType;               /*!< Descriptor type. */
    const cdc_desc_subtype_t bDescriptorSubtype; /*!< Descriptor subtype. */
    union {
        struct {
            uint8_t feature:    1; /*!< Supports Set, Clear, and Get Comm Feature requests. */
            uint8_t serial:     1; /*!< Supports line coding and control line requests plus serial state reporting. */
            uint8_t send_break: 1; /*!< Supports the Send Break request. */
            uint8_t network:    1; /*!< Supports the Network Connection notification. */
            uint8_t reserved:   4; /*!< Reserved bits. */
        };
        uint8_t val;                          /*!< Raw capabilities bitmap. */
    } bmCapabilities;                        /*!< Abstract control management capabilities. */
} __attribute__((packed)) cdc_acm_acm_desc_t;

/**
 * @brief Line coding structure.
 *
 * @see Table 17, USB CDC-PSTN specification rev. 1.2
 */
typedef struct {
    uint32_t dwDTERate;  /*!< Data terminal rate in bits per second. */
    uint8_t bCharFormat; /*!< Stop bit format: 0 for 1, 1 for 1.5, 2 for 2 stop bits. */
    uint8_t bParityType; /*!< Parity type: 0 none, 1 odd, 2 even, 3 mark, 4 space. */
    uint8_t bDataBits;   /*!< Data bits per character: 5, 6, 7, 8, or 16. */
} __attribute__((packed)) cdc_acm_line_coding_t;

/**
 * @brief UART State Bitmap
 *
 * This state bitmap is based on USB CDC-PSTN specification.
 * It was extended with CTS signal, which is the only missing input signal from RS-232 set.
 *
 * @see Table 31, USB CDC-PSTN specification rev. 1.2
 */
typedef struct {
    union {
        struct {
            uint16_t bRxCarrier : 1;   /*!< Receiver carrier state. Maps to V.24 signal 109 and RS-232 DCD. */
            uint16_t bTxCarrier : 1;   /*!< Transmission carrier state. Maps to V.24 signal 106 and RS-232 DSR. */
            uint16_t bBreak : 1;       /*!< Break detection state. */
            uint16_t bRingSignal : 1;  /*!< Ring signal detection state. */
            uint16_t bFraming : 1;     /*!< Framing error indicator. */
            uint16_t bParity : 1;      /*!< Parity error indicator. */
            uint16_t bOverRun : 1;     /*!< Receive overrun indicator. */
            uint16_t reserved : 8;     /*!< Reserved bits. */
            uint16_t bClearToSend : 1; /*!< Clear To Send state. This bit is not defined by the USB specification. */
        };
        uint16_t val;                  /*!< Raw UART state bitmap. */
    };
} cdc_acm_uart_state_t;
