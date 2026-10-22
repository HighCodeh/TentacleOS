// SPDX-License-Identifier: GPL-3.0-or-later
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "hb_muse_account_fetch.h"
#include "vm_api.h"
#include "vm_connect.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t bounded_length(const char *s, size_t cap) {
  if (!s)
    return cap;
  size_t n = 0;
  while (n < cap && s[n])
    ++n;
  return n;
}
static void wipe(void *memory, size_t length) {
  volatile unsigned char *p = memory;
  while (length--)
    *p++ = 0;
}
static void wipe_token(char *token) {
  if (token)
    wipe(token, bounded_length(token, HB_MUSE_ACCOUNT_TOKEN_CAP));
}
void hb_muse_account_result_clear(hb_muse_account_result_t *result) {
  if (!result)
    return;
  wipe_token(result->token);
  free(result->token);
  memset(result, 0, sizeof(*result));
}
static bool printable(const char *s, size_t cap, bool empty) {
  size_t n = bounded_length(s, cap);
  if (n == cap || (!empty && !n))
    return false;
  for (size_t i = 0; i < n; ++i)
    if ((unsigned char)s[i] < 0x21u || (unsigned char)s[i] > 0x7eu)
      return false;
  return true;
}
static bool valid_id(const char *id, bool empty) {
  size_t n = bounded_length(id, HB_MUSE_ACCOUNT_ID_CAP);
  if (n == HB_MUSE_ACCOUNT_ID_CAP || (!empty && !n))
    return false;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  }
  return true;
}
static bool valid_name(const char *name) {
  if (!name)
    return true;
  size_t n = bounded_length(name, HB_MUSE_ACCOUNT_NAME_CAP);
  if (n == HB_MUSE_ACCOUNT_NAME_CAP)
    return false;
  for (size_t i = 0; i < n;) {
    uint32_t cp = (unsigned char)name[i++];
    if (cp < 0x80u) {
      if (cp < 0x20u || cp == 0x7fu)
        return false;
      continue;
    }
    unsigned needed;
    uint32_t minimum;
    if (cp >= 0xc2u && cp <= 0xdfu) {
      needed = 1;
      minimum = 0x80;
      cp &= 0x1fu;
    } else if (cp >= 0xe0u && cp <= 0xefu) {
      needed = 2;
      minimum = 0x800;
      cp &= 0x0fu;
    } else if (cp >= 0xf0u && cp <= 0xf4u) {
      needed = 3;
      minimum = 0x10000;
      cp &= 0x07u;
    } else
      return false;
    if ((size_t)needed > n - i)
      return false;
    for (unsigned j = 0; j < needed; ++j) {
      unsigned char c = (unsigned char)name[i++];
      if ((c & 0xc0u) != 0x80u)
        return false;
      cp = (cp << 6) | (uint32_t)(c & 0x3fu);
    }
    if (cp < minimum || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu) ||
        (cp >= 0x80u && cp <= 0x9fu))
      return false;
  }
  return true;
}
static bool valid_url(const char *url) {
  if (!printable(url, HB_MUSE_ACCOUNT_URL_CAP, false) || strncmp(url, "wss://", 6) || !url[6] ||
      strchr(url, '#') || strchr(url, '@') || strchr(url, '\\'))
    return false;
  const char *p = url + 6;
  size_t label = 0;
  char previous = 0;
  while (*p && *p != ':' && *p != '/' && *p != '?') {
    unsigned char c = (unsigned char)*p;
    if (c == '.') {
      if (!label || previous == '-')
        return false;
      label = 0;
    } else {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-') ||
          (!label && c == '-') || ++label > 63u)
        return false;
    }
    previous = *p++;
  }
  if (!label || previous == '-')
    return false;
  if (*p == ':') {
    unsigned port = 0, digits = 0;
    ++p;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 5u)
        return false;
      port = port * 10u + (unsigned)(*p++ - '0');
    }
    if (!digits || !port || port > 65535u || (*p && *p != '/' && *p != '?'))
      return false;
  }
  for (; *p; ++p) {
    if (*p != '%')
      continue;
    for (unsigned digit = 0; digit < 2; ++digit) {
      unsigned char c = (unsigned char)*++p;
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        return false;
    }
  }
  return true;
}
static bool valid_access(const char *token) {
  return printable(token, HB_MUSE_ACCOUNT_TOKEN_CAP, false) &&
         strlen(token) <= HB_MUSE_ACCOUNT_TOKEN_CAP - sizeof("Bearer ");
}
static bool valid_refresh(const char *token) {
  if (!printable(token, HB_MUSE_ACCOUNT_TOKEN_CAP, false))
    return false;
  const char *raw = strrchr(token, ':');
  raw = raw ? raw + 1 : token;
  return *raw && strlen(raw) <= HB_MUSE_ACCOUNT_TOKEN_CAP - sizeof("Bearer hatch_refresh:");
}
static void free_list(vm_info_t *vms) {
  for (int i = 0; i < VM_API_MAX_VMS; ++i)
    wipe_token(vms[i].vm_auth_token);
  /* Array is zero-initialized, including unused entries. SDK failure cleanup
   * also nulls freed entries, so this safely frees partial/fake API results. */
  vm_list_free(vms, VM_API_MAX_VMS);
  memset(vms, 0, sizeof(vm_info_t) * VM_API_MAX_VMS);
}
static hb_muse_account_status_t select_vm(vm_info_t *vms,
                                          int count,
                                          const char *wanted,
                                          const char *preferred,
                                          hb_muse_account_result_t *result) {
  const vm_info_t *selected = NULL;
  unsigned matches = 0;
  for (int i = 0; i < count; ++i) {
    const vm_info_t *entry = &vms[i];
    char id[HB_MUSE_ACCOUNT_ID_CAP] = {0};
    const char *token = NULL;
    if (!valid_url(entry->vm_url) || !valid_name(entry->vm_name) ||
        (entry->vm_id && !valid_id(entry->vm_id, true)) ||
        !printable(entry->vm_auth_token, HB_MUSE_ACCOUNT_TOKEN_CAP, false) ||
        !vm_connect_params(entry, id, sizeof(id), &token) || !valid_id(id, false))
      return HB_MUSE_ACCOUNT_IO;
    bool match = wanted[0]      ? !strcmp(id, wanted)
                 : preferred[0] ? !strcmp(entry->vm_url, preferred)
                                : false;
    if (match) {
      selected = entry;
      ++matches;
    }
  }
  if (matches > 1)
    return HB_MUSE_ACCOUNT_IO;
  if (!wanted[0] && !preferred[0])
    selected = vm_find_default(vms, count);
  if (!selected)
    return HB_MUSE_ACCOUNT_NO_VM;
  const char *token = NULL;
  if (!vm_connect_params(selected, result->id, sizeof(result->id), &token))
    return HB_MUSE_ACCOUNT_IO;
  size_t token_len = strlen(token);
  result->token = calloc(token_len + 1, 1);
  if (!result->token)
    return HB_MUSE_ACCOUNT_OOM;
  memcpy(result->token, token, token_len + 1);
  if (selected->vm_name)
    memcpy(result->name, selected->vm_name, strlen(selected->vm_name) + 1);
  return HB_MUSE_ACCOUNT_OK;
}

hb_muse_account_status_t hb_muse_account_fetch(const hb_muse_account_ops_t *ops,
                                               void *user,
                                               const char *want_vm,
                                               hb_muse_account_result_t *result) {
  if (!result)
    return HB_MUSE_ACCOUNT_IO;
  /* Caller owns any previous result; do not silently leak or free it. */
  if (result->token)
    return HB_MUSE_ACCOUNT_IO;
  memset(result, 0, sizeof(*result));
  if (!ops || !ops->read_str || !ops->current || !ops->store_pair || !ops->identity)
    return HB_MUSE_ACCOUNT_IO;
  if (!ops->current(user))
    return HB_MUSE_ACCOUNT_STALE;
  const char *wanted = want_vm ? want_vm : "";
  if (!valid_id(wanted, true))
    return HB_MUSE_ACCOUNT_IO;
  char preferred[HB_MUSE_ACCOUNT_URL_CAP] = {0};
  if (!wanted[0]) {
    hb_muse_account_read_t read = ops->read_str(user, "vm_url", preferred, sizeof(preferred));
    if (!ops->current(user))
      return HB_MUSE_ACCOUNT_STALE;
    if (read != HB_MUSE_ACCOUNT_READ_OK && read != HB_MUSE_ACCOUNT_READ_MISSING)
      return HB_MUSE_ACCOUNT_IO;
    if (read == HB_MUSE_ACCOUNT_READ_MISSING)
      preferred[0] = '\0';
    else if (preferred[0] && !valid_url(preferred))
      return HB_MUSE_ACCOUNT_IO;
  }
  char *access = calloc(HB_MUSE_ACCOUNT_TOKEN_CAP, 1);
  if (!access)
    return HB_MUSE_ACCOUNT_OOM;
  char *refresh = NULL;
  vm_info_t vms[VM_API_MAX_VMS] = {0};
  vm_device_tokens_t replacement = {0};
  hb_muse_account_status_t status = HB_MUSE_ACCOUNT_IO;
  hb_muse_account_read_t read =
      ops->read_str(user, "access_token", access, HB_MUSE_ACCOUNT_TOKEN_CAP);
  if (!ops->current(user)) {
    status = HB_MUSE_ACCOUNT_STALE;
    goto done;
  }
  if (read == HB_MUSE_ACCOUNT_READ_MISSING || (read == HB_MUSE_ACCOUNT_READ_OK && !access[0])) {
    status = HB_MUSE_ACCOUNT_UNAVAILABLE;
    goto done;
  }
  if (read != HB_MUSE_ACCOUNT_READ_OK || !valid_access(access))
    goto done;
  if (!ops->current(user)) {
    status = HB_MUSE_ACCOUNT_STALE;
    goto done;
  }
  int count = vm_api_fetch_vms(access, vms, VM_API_MAX_VMS);
  if (!ops->current(user)) {
    status = HB_MUSE_ACCOUNT_STALE;
    goto done;
  }
  if (count == VM_API_ERR_AUTH) {
    free_list(vms);
    refresh = calloc(HB_MUSE_ACCOUNT_TOKEN_CAP, 1);
    if (!refresh) {
      status = HB_MUSE_ACCOUNT_OOM;
      goto done;
    }
    read = ops->read_str(user, "refresh_token", refresh, HB_MUSE_ACCOUNT_TOKEN_CAP);
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
    if (read == HB_MUSE_ACCOUNT_READ_MISSING || (read == HB_MUSE_ACCOUNT_READ_OK && !refresh[0])) {
      status = HB_MUSE_ACCOUNT_AUTH;
      goto done;
    }
    if (read != HB_MUSE_ACCOUNT_READ_OK || !valid_refresh(refresh))
      goto done;
    char device_id[HB_MUSE_ACCOUNT_ID_CAP] = {0};
    bool identity_ok = ops->identity(user, device_id, sizeof(device_id));
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
    if (!identity_ok) {
      status = HB_MUSE_ACCOUNT_UNAVAILABLE;
      goto done;
    }
    if (!valid_id(device_id, false))
      goto done;
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
    vm_token_refresh_method_t method = VM_TOKEN_REFRESH_NONE;
    int rc = vm_api_refresh_device_token(NULL, refresh, device_id, &replacement, &method);
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
    if (rc != 0) {
      status = rc == VM_API_ERR_AUTH ? HB_MUSE_ACCOUNT_AUTH : HB_MUSE_ACCOUNT_IO;
      goto done;
    }
    if (method != VM_TOKEN_REFRESH_REFRESH_TOKEN || !valid_access(replacement.access_token) ||
        !valid_refresh(replacement.refresh_token))
      goto done;
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
    bool committed = ops->store_pair(
        user, replacement.access_token, replacement.refresh_token, ops->current, user);
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
    if (!committed)
      goto done;
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
    count = vm_api_fetch_vms(replacement.access_token, vms, VM_API_MAX_VMS);
    if (!ops->current(user)) {
      status = HB_MUSE_ACCOUNT_STALE;
      goto done;
    }
  }
  if (count < 0 || count > VM_API_MAX_VMS) {
    status = count == VM_API_ERR_AUTH ? HB_MUSE_ACCOUNT_AUTH : HB_MUSE_ACCOUNT_IO;
    goto done;
  }
  status = select_vm(vms, count, wanted, preferred, result);
  if (!ops->current(user))
    status = HB_MUSE_ACCOUNT_STALE;
done:
  free_list(vms);
  wipe_token(replacement.access_token);
  wipe_token(replacement.refresh_token);
  vm_device_tokens_free(&replacement);
  if (access) {
    wipe(access, HB_MUSE_ACCOUNT_TOKEN_CAP);
    free(access);
  }
  if (refresh) {
    wipe(refresh, HB_MUSE_ACCOUNT_TOKEN_CAP);
    free(refresh);
  }
  if (status != HB_MUSE_ACCOUNT_OK)
    hb_muse_account_result_clear(result);
  return status;
}
