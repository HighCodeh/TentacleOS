/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "vm_api.h"

// Which VM we connect to, and with which credential. Pure, so the host tests
// can run the decision rather than inspect it.
//
// The id and unchanged token must come from the same fetch_vms entry. Refuse
// an entry that cannot supply both: neither an account-wide credential nor
// another VM's token is a valid substitute for this VM's token.

// Pull the VM id out of a wss://<id>.<domain>/ URL. Restricted to the
// characters legal in a hostname label, because the result feeds the WSS
// request target.
static inline size_t vm_derive_id(const char *vm_url, char *out,
                                  size_t out_cap) {
    if (!vm_url || !out || out_cap == 0) return 0;
    const char *p = strstr(vm_url, "://");
    if (!p) return 0;
    p += 3;
    const char *dot = strchr(p, '.');
    if (!dot) return 0;
    size_t id_len = (size_t)(dot - p);
    if (id_len + 1 > out_cap || id_len == 0) return 0;
    if (strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                  "0123456789-") != id_len) {
        return 0;
    }
    memcpy(out, p, id_len);
    out[id_len] = '\0';
    return id_len;
}

// Resolve the (vm_id, auth_token) pair for one fetch_vms entry.
//
// Returns false when the entry cannot supply both, and writes nothing. That is
// the whole no-fallback rule: there is no second credential to reach for, so
// the only correct answer is to refuse the connection. *token_out always
// aliases this entry's own token — never a copy, never another entry's.
static inline bool vm_connect_params(const vm_info_t *target,
                                     char *vm_id_out, size_t vm_id_cap,
                                     const char **token_out) {
    if (!target || !vm_id_out || vm_id_cap == 0 || !token_out) return false;
    if (!target->vm_url || !target->vm_auth_token) return false;
    if (!*target->vm_auth_token) return false;

    if (target->vm_id && *target->vm_id) {
        size_t len = strlen(target->vm_id);
        // Refuse rather than truncate: a clipped id can name a different VM,
        // and we would then present this VM's token against it.
        if (len + 1 > vm_id_cap) return false;
        memcpy(vm_id_out, target->vm_id, len + 1);
    } else if (vm_derive_id(target->vm_url, vm_id_out, vm_id_cap) == 0) {
        return false;
    }

    *token_out = target->vm_auth_token;
    return true;
}
