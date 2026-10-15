#ifndef HB_MUSE_PAIRING_FRAGMENTS_H
#define HB_MUSE_PAIRING_FRAGMENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HB_MUSE_PAIRING_ATT_MAX         512u
#define HB_MUSE_PAIRING_NOTIFY_MAX      160u
#define HB_MUSE_PAIRING_FRAGMENT_TTL_MS 5000u
typedef struct {
  uint8_t *buffer;
  size_t bytes;
  uint64_t connection, started_ms, last_ms;
  uint8_t total, next;
  bool clock_started, clock_fault;
} hb_muse_pairing_fragments_t;
typedef enum {
  HB_MUSE_FRAGMENT_BAD,
  HB_MUSE_FRAGMENT_MORE,
  HB_MUSE_FRAGMENT_READY
} hb_muse_fragment_result_t;
/* Zero initialize once. One INTERNAL-RAM worker owns codec. Drop on disconnect,
 * consent change, queue overflow or missing fragment. No RTOS/crypto calls. */
void hb_muse_pairing_fragments_drop(hb_muse_pairing_fragments_t *state);
void hb_muse_pairing_fragments_expire(hb_muse_pairing_fragments_t *state, uint64_t now_ms);
/* READY transfers heap ownership of NUL-terminated complete JSON to caller.
 * Always release via release(), even rejected messages. Non-READY writes NULL/0.
 * Exact SDK framing [0xfe,index,total,payload], or one raw JSON ATT write.
 * Connection is a unique local generation, NEVER a reused ATT connection ID. */
hb_muse_fragment_result_t hb_muse_pairing_fragments_feed(hb_muse_pairing_fragments_t *state,
                                                         uint64_t connection,
                                                         const uint8_t *fragment,
                                                         size_t bytes,
                                                         uint64_t now_ms,
                                                         uint8_t **complete,
                                                         size_t *complete_bytes);
void hb_muse_pairing_fragments_release(uint8_t *json, size_t bytes);
/* Pure bounded TX codec. Call only from worker. Revalidate connection/window
 * AND link_pairing_record_session_is_current before sending EVERY fragment. */
bool hb_muse_pairing_fragment_encode(const uint8_t *json,
                                     size_t bytes,
                                     uint16_t mtu,
                                     uint8_t index,
                                     uint8_t out[HB_MUSE_PAIRING_NOTIFY_MAX],
                                     size_t *out_bytes,
                                     uint8_t *total);
#endif
