#ifndef MF_KEY_DICT_H
#define MF_KEY_DICT_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum user-added keys stored in NVS (on top of built-in). */
#define MF_KEY_DICT_MAX_EXTRA   128

/* Total built-in keys count (defined in .c). */
extern const int MF_KEY_DICT_BUILTIN_COUNT;

void         mf_key_dict_init(void);
int          mf_key_dict_count(void);
void         mf_key_dict_get(int idx, uint8_t key_out[6]);
bool         mf_key_dict_contains(const uint8_t key[6]);
hb_nfc_err_t mf_key_dict_add(const uint8_t key[6]);

#ifdef __cplusplus
}
#endif
#endif
