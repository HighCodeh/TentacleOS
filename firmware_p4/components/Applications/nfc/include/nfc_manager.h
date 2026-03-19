/**
 * @file nfc_manager.h
 * @brief NFC Manager high-level FSM + FreeRTOS task.
 *
 * Coordinates scan detect read action pipeline.
 */
#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include "highboy_nfc.h"
#include "highboy_nfc_types.h"

typedef enum {
    NFC_STATE_IDLE = 0,
    NFC_STATE_SCANNING,
    NFC_STATE_READING,
    NFC_STATE_EMULATING,
    NFC_STATE_ERROR,
} nfc_state_t;

typedef void (*nfc_card_found_cb_t)(const hb_nfc_card_data_t* card, void* ctx);

/**
 * Start the NFC manager scan task.
 * Hardware must be pre-initialized with highboy_nfc_init().
 */
hb_nfc_err_t nfc_manager_start(nfc_card_found_cb_t cb, void* ctx);

/** Stop the NFC manager. */
void nfc_manager_stop(void);

/** Get current state. */
nfc_state_t nfc_manager_get_state(void);

#endif
