/**
 * @file highboy_nfc.h
 * @brief High Boy NFC Library Public API.
 *
 * Proven working config (ESP32-P4 + ST25R3916):
 *  MOSI=18, MISO=19, SCK=17, CS=3, IRQ=8
 *  SPI Mode 1, 500 kHz, cs_ena_pretrans=1, cs_ena_posttrans=1
 */
#ifndef HIGHBOY_NFC_H
#define HIGHBOY_NFC_H

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

typedef struct {
    
    int pin_mosi;       
    int pin_miso;       
    int pin_sclk;       
    int pin_cs;         
    
    int pin_irq;        
    
    int spi_host;       
    int spi_mode;       
    uint32_t spi_clock_hz;  
} highboy_nfc_config_t;

#define HIGHBOY_NFC_CONFIG_DEFAULT() { \
    .pin_mosi     = 18,                \
    .pin_miso     = 19,                \
    .pin_sclk     = 17,                \
    .pin_cs       = 3,                 \
    .pin_irq      = 8,                 \
    .spi_host     = 2,                 \
    .spi_mode     = 1,                 \
    .spi_clock_hz = 500000,            \
}

hb_nfc_err_t highboy_nfc_init(const highboy_nfc_config_t* config);
void         highboy_nfc_deinit(void);
hb_nfc_err_t highboy_nfc_ping(uint8_t* chip_id);

hb_nfc_err_t highboy_nfc_field_on(void);
void         highboy_nfc_field_off(void);
uint8_t      highboy_nfc_measure_amplitude(void);
bool         highboy_nfc_field_detected(uint8_t* aux_display);

#endif
