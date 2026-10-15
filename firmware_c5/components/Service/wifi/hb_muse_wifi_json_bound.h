/* Pre-parser stack bound for existing known-network JSON. Public helper only;
 * no credentials, mutation, parser allocation or changes to global cJSON. */
#ifndef HB_MUSE_WIFI_JSON_BOUND_H
#define HB_MUSE_WIFI_JSON_BOUND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static inline bool hb_muse_wifi_json_shallow(const uint8_t *bytes, size_t n) {
  if (!bytes || !n || n > 32768u)
    return false;
  unsigned depth = 0;
  bool quoted = false, escaped = false;
  for (size_t i = 0; i < n; ++i) {
    uint8_t c = bytes[i];
    if (!c)
      return false;
    if (quoted) {
      if (c < 0x20u)
        return false;
      if (escaped) {
        /* cJSON otherwise embeds a NUL and later strlen/strcmp sees
         * only the prefix of an SSID/key/password. */
        if (c == 'u' && i + 4u < n && bytes[i + 1u] == '0' && bytes[i + 2u] == '0' &&
            bytes[i + 3u] == '0' && bytes[i + 4u] == '0')
          return false;
        escaped = false;
      } else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (++depth > 2u)
        return false; /* array -> entry object -> scalar */
    } else if (c == '}' || c == ']') {
      if (!depth)
        return false;
      --depth;
    }
  }
  /* This bounds recursion only. cJSON still validates complete JSON, array
   * root, escaped syntax, object fields, duplicates and trailing content. */
  return depth == 0 && !quoted && !escaped;
}
#endif
