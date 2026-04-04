#ifndef NFC_STORE_H
#define NFC_STORE_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic NFC card dump storage — protocol-agnostic NVS persistence.
 *
 * NVS namespace: "nfc_store"
 *   "cnt"   u8     — number of stored entries
 *   "n_N"   string — entry N name
 *   "p_N"   u8     — entry N protocol (hb_nfc_protocol_t)
 *   "u_N"   blob   — entry N UID (uid_len bytes)
 *   "a_N"   blob   — entry N ATQA (2 bytes) + SAK (1 byte)
 *   "d_N"   blob   — entry N payload (protocol-specific dump)
 *
 * Payload formats:
 *   HB_PROTO_MF_ULTRALIGHT: [version:8][page_count:2LE][pages: page_count*4]
 *   HB_PROTO_ISO15693:      [dsfid:1][block_size:1][block_count:2LE][blocks...]
 *   Others:                 raw blob (e.g., ATS, or just uid/atqa for reference)
 */

#define NFC_STORE_MAX_ENTRIES  16
#define NFC_STORE_NAME_MAX     32
#define NFC_STORE_PAYLOAD_MAX  2048

typedef struct {
    char              name[NFC_STORE_NAME_MAX];
    hb_nfc_protocol_t protocol;
    uint8_t           uid[10];
    uint8_t           uid_len;
    uint8_t           atqa[2];
    uint8_t           sak;
    size_t            payload_len;
    uint8_t           payload[NFC_STORE_PAYLOAD_MAX];
} nfc_store_entry_t;

/* Light info struct — no payload, for listing */
typedef struct {
    char              name[NFC_STORE_NAME_MAX];
    hb_nfc_protocol_t protocol;
    uint8_t           uid[10];
    uint8_t           uid_len;
} nfc_store_info_t;

void         nfc_store_init(void);
int          nfc_store_count(void);
hb_nfc_err_t nfc_store_save(const char*       name,
                              hb_nfc_protocol_t protocol,
                              const uint8_t*    uid,
                              uint8_t           uid_len,
                              const uint8_t     atqa[2],
                              uint8_t           sak,
                              const uint8_t*    payload,
                              size_t            payload_len);
hb_nfc_err_t nfc_store_load(int index, nfc_store_entry_t* out);
hb_nfc_err_t nfc_store_get_info(int index, nfc_store_info_t* out);
int          nfc_store_list(nfc_store_info_t* infos, int max);
hb_nfc_err_t nfc_store_delete(int index);
hb_nfc_err_t nfc_store_find_by_uid(const uint8_t* uid, uint8_t uid_len, int* index_out);

/* Convenience: build NTAG/Ultralight payload from version + pages */
size_t nfc_store_pack_mful(const uint8_t version[8],
                            const uint8_t* pages,
                            uint16_t       page_count,
                            uint8_t*       out,
                            size_t         out_max);

/* Convenience: unpack NTAG/Ultralight payload */
bool nfc_store_unpack_mful(const uint8_t* payload,
                            size_t         payload_len,
                            uint8_t        version_out[8],
                            uint8_t*       pages_out,
                            uint16_t*      page_count_out,
                            uint16_t       pages_out_max);

#ifdef __cplusplus
}
#endif
#endif /* NFC_STORE_H */
