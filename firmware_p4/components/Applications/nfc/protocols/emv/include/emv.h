/**
 * @file emv.h
 * @brief EMV contactless helpers (PPSE, AID select, GPO, READ RECORD).
 */
#ifndef EMV_H
#define EMV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

#define EMV_AID_MAX_LEN      16U
#define EMV_APP_LABEL_MAX    16U

typedef struct {
    uint8_t aid[EMV_AID_MAX_LEN];
    uint8_t aid_len;
    char    label[EMV_APP_LABEL_MAX + 1U];
} emv_app_t;

typedef struct {
    uint8_t sfi;
    uint8_t record_first;
    uint8_t record_last;
    uint8_t offline_auth_records;
} emv_afl_entry_t;

typedef void (*emv_record_cb_t)(uint8_t sfi,
                                uint8_t record,
                                const uint8_t* data,
                                size_t len,
                                void* user);

hb_nfc_err_t emv_select_ppse(hb_nfc_protocol_t proto,
                            const void* ctx,
                            uint8_t* fci, size_t fci_max, size_t* fci_len,
                            uint16_t* sw,
                            int timeout_ms);

size_t emv_extract_aids(const uint8_t* fci, size_t fci_len,
                        emv_app_t* out, size_t max);

hb_nfc_err_t emv_select_aid(hb_nfc_protocol_t proto,
                           const void* ctx,
                           const uint8_t* aid, size_t aid_len,
                           uint8_t* fci, size_t fci_max, size_t* fci_len,
                           uint16_t* sw,
                           int timeout_ms);

hb_nfc_err_t emv_get_processing_options(hb_nfc_protocol_t proto,
                                        const void* ctx,
                                        const uint8_t* pdol, size_t pdol_len,
                                        uint8_t* gpo, size_t gpo_max, size_t* gpo_len,
                                        uint16_t* sw,
                                        int timeout_ms);

size_t emv_parse_afl(const uint8_t* gpo, size_t gpo_len,
                     emv_afl_entry_t* out, size_t max);

hb_nfc_err_t emv_read_record(hb_nfc_protocol_t proto,
                             const void* ctx,
                             uint8_t sfi,
                             uint8_t record,
                             uint8_t* out, size_t out_max, size_t* out_len,
                             uint16_t* sw,
                             int timeout_ms);

hb_nfc_err_t emv_read_records(hb_nfc_protocol_t proto,
                              const void* ctx,
                              const emv_afl_entry_t* afl, size_t afl_count,
                              emv_record_cb_t cb, void* user,
                              int timeout_ms);

hb_nfc_err_t emv_run_basic(hb_nfc_protocol_t proto,
                           const void* ctx,
                           emv_app_t* app_out,
                           emv_record_cb_t cb, void* user,
                           int timeout_ms);

#endif /* EMV_H */
