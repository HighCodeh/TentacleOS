/**
 * @file t4t.h
 * @brief ISO14443-4 / Type 4 Tag (T4T) NDEF reader helpers.
 */
#ifndef T4T_H
#define T4T_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

typedef struct {
    uint16_t cc_len;
    uint16_t mle;
    uint16_t mlc;
    uint16_t ndef_fid;
    uint16_t ndef_max;
    uint8_t  read_access;
    uint8_t  write_access;
} t4t_cc_t;

/** Read and parse Capability Container (CC) file. */
hb_nfc_err_t t4t_read_cc(const nfc_iso_dep_data_t* dep, t4t_cc_t* cc);

/** Read NDEF file using CC information. */
hb_nfc_err_t t4t_read_ndef(const nfc_iso_dep_data_t* dep,
                            const t4t_cc_t*         cc,
                            uint8_t*               out,
                            size_t                 out_max,
                            size_t*                out_len);

/** Write NDEF file (updates NLEN + payload). */
hb_nfc_err_t t4t_write_ndef(const nfc_iso_dep_data_t* dep,
                             const t4t_cc_t*         cc,
                             const uint8_t*         data,
                             size_t                 data_len);

/** Read binary from NDEF file (raw access, no NLEN handling). */
hb_nfc_err_t t4t_read_binary_ndef(const nfc_iso_dep_data_t* dep,
                                   const t4t_cc_t*         cc,
                                   uint16_t               offset,
                                   uint8_t*               out,
                                   size_t                 out_len);

/** Update binary in NDEF file (raw access, no NLEN handling). */
hb_nfc_err_t t4t_update_binary_ndef(const nfc_iso_dep_data_t* dep,
                                     const t4t_cc_t*         cc,
                                     uint16_t               offset,
                                     const uint8_t*         data,
                                     size_t                 data_len);

#endif /* T4T_H */
