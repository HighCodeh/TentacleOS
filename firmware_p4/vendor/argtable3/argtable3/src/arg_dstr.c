/*******************************************************************************
 * arg_dstr: Implements the dynamic string utilities
 *
 * This file is part of the argtable3 library.
 *
 * Copyright (C) 2013-2019 Tom G. Huang
 * <tomghuang@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of STEWART HEITMANN nor the  names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL STEWART HEITMANN BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#include "argtable3.h"

#ifndef ARG_AMALGAMATION
#include "argtable3_private.h"
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

#define START_VSNBUFF 16

#if defined(_MSC_VER)
    #define arg_vsnprintf _vsnprintf
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    #define arg_vsnprintf vsnprintf
#else
/**
 * A C89-compatible replacement for vsnprintf().
 *
 * Writes at most (size - 1) characters to str and always null-terminates,
 * unless size is 0. The output is truncated if necessary.
 *
 * @param str  Output buffer.
 * @param size Size of output buffer.
 * @param format printf-style format string.
 * @param ap    va_list of arguments.
 * @return The number of characters that would have been written, excluding the null byte.
 */
int arg_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    char *temp_buffer;
    int result;
    size_t temp_size;

    if (str == NULL || size == 0) {
        /* Just calculate required size using a temporary large buffer */
        temp_size = 65536; /* Large temporary buffer */
        temp_buffer = malloc(temp_size);
        if (temp_buffer == NULL) {
            return -1;
        }

        result = vsprintf(temp_buffer, format, ap);
        free(temp_buffer);
        return result;
    }

    if (size == 1) {
        str[0] = '\0';
        return 0;
    }

    /* Use a large temporary buffer to safely format */
    temp_size = size * 4; /* Start with 4x the target size */
    if (temp_size < 1024) {
        temp_size = 1024;
    }

    temp_buffer = malloc(temp_size);
    if (temp_buffer == NULL) {
        return -1;
    }

    result = vsprintf(temp_buffer, format, ap);
    if (result < 0) {
        free(temp_buffer);
        return -1;
    }

    /* Copy to destination buffer, truncating if necessary */
    strncpy(str, temp_buffer, size - 1);
    if ((size_t)result >= size) {
        /* Truncate */
        str[size - 1] = '\0';
    }

    free(temp_buffer);
    return result; /* Return the number of chars that would be written */
}
#endif

/*
 * This dynamic string module is adapted from TclResult.c in the Tcl library.
 * Here is the copyright notice from the library:
 *
 * This software is copyrighted by the Regents of the University of
 * California, Sun Microsystems, Inc., Scriptics Corporation, ActiveState
 * Corporation and other parties.  The following terms apply to all files
 * associated with the software unless explicitly disclaimed in
 * individual files.
 *
 * The authors hereby grant permission to use, copy, modify, distribute,
 * and license this software and its documentation for any purpose, provided
 * that existing copyright notices are retained in all copies and that this
 * notice is included verbatim in any distributions. No written agreement,
 * license, or royalty fee is required for any of the authorized uses.
 * Modifications to this software may be copyrighted by their authors
 * and need not follow the licensing terms described here, provided that
 * the new terms are clearly indicated on the first page of each file where
 * they apply.
 *
 * IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY PARTY
 * FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY
 * DERIVATIVES THEREOF, EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.  THIS SOFTWARE
 * IS PROVIDED ON AN "AS IS" BASIS, AND THE AUTHORS AND DISTRIBUTORS HAVE
 * NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
 * MODIFICATIONS.
 *
 * GOVERNMENT USE: If you are acquiring this software on behalf of the
 * U.S. government, the Government shall have only "Restricted Rights"
 * in the software and related documentation as defined in the Federal
 * Acquisition Regulations (FARs) in Clause 52.227.19 (c) (2).  If you
 * are acquiring the software on behalf of the Department of Defense, the
 * software shall be classified as "Commercial Computer Software" and the
 * Government shall have only "Restricted Rights" as defined in Clause
 * 252.227-7014 (b) (3) of DFARs.  Notwithstanding the foregoing, the
 * authors grant the U.S. Government and others acting in its behalf
 * permission to use and distribute the software in accordance with the
 * terms specified in this license.
 */

typedef struct _internal_arg_dstr {
    char* data;
    arg_dstr_freefn* free_proc;
    char sbuf[ARG_DSTR_SIZE + 1];
    char* append_data;
    int append_data_size;
    int append_used;
} _internal_arg_dstr_t;

static void setup_append_buf(arg_dstr_t res, int newSpace);

arg_dstr_t arg_dstr_create(void) {
    _internal_arg_dstr_t* h = (_internal_arg_dstr_t*)xmalloc(sizeof(_internal_arg_dstr_t));
    memset(h, 0, sizeof(_internal_arg_dstr_t));
    h->sbuf[0] = 0;
    h->data = h->sbuf;
    h->free_proc = ARG_DSTR_STATIC;
    return h;
}

void arg_dstr_destroy(arg_dstr_t ds) {
    if (ds == NULL)
        return;

    arg_dstr_reset(ds);
    xfree(ds);
    return;
}

void arg_dstr_set(arg_dstr_t ds, char* str, arg_dstr_freefn* free_proc) {
    int length;
    register arg_dstr_freefn* old_free_proc = ds->free_proc;
    char* old_result = ds->data;

    if (str == NULL) {
        ds->sbuf[0] = 0;
        ds->data = ds->sbuf;
        ds->free_proc = ARG_DSTR_STATIC;
    } else if (free_proc == ARG_DSTR_VOLATILE) {
        length = (int)strlen(str);
        if (length > ARG_DSTR_SIZE) {
            ds->data = (char*)xmalloc((size_t)length + 1);
            ds->free_proc = ARG_DSTR_DYNAMIC;
            strncpy(ds->data, str, (size_t)length + 1); /* NOSONAR */
            assert(ds->data[length] == '\0');
        } else {
            ds->data = ds->sbuf;
            ds->free_proc = ARG_DSTR_STATIC;
            strncpy(ds->data, str, sizeof(ds->sbuf) - 1); /* NOSONAR */
            ds->data[sizeof(ds->sbuf) - 1] = '\0';
            assert(ds->data[ARG_DSTR_SIZE] == '\0');
        }
    } else {
        ds->data = str;
        ds->free_proc = free_proc;
    }

    /*
     * If the old result was dynamically-allocated, free it up. Do it here,
     * rather than at the beginning, in case the new result value was part of
     * the old result value.
     */

    if ((old_free_proc != 0) && (old_result != ds->data)) {
        if (old_free_proc == ARG_DSTR_DYNAMIC) {
            xfree(old_result);
        } else {
            (*old_free_proc)(old_result);
        }
    }

    if ((ds->append_data != NULL) && (ds->append_data_size > 0)) {
        xfree(ds->append_data);
        ds->append_data = NULL;
        ds->append_data_size = 0;
    }
}

char* arg_dstr_cstr(arg_dstr_t ds) /* Interpreter whose result to return. */
{
    return ds->data;
}

void arg_dstr_cat(arg_dstr_t ds, const char* str) {
    setup_append_buf(ds, (int)strlen(str) + 1);
    memcpy(ds->data + strlen(ds->data), str, strlen(str));
}

void arg_dstr_catc(arg_dstr_t ds, char c) {
    setup_append_buf(ds, 2);
    memcpy(ds->data + strlen(ds->data), &c, 1);
}

/*
 * The logic of the `arg_dstr_catf` function is adapted from the `bformat`
 * function in The Better String Library by Paul Hsieh. Here is the copyright
 * notice from the library:
 *
 * Copyright (c) 2014, Paul Hsieh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of bstrlib nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
void arg_dstr_catf(arg_dstr_t ds, const char* fmt, ...) {
    va_list arglist;
    char* buff;
    int n, r;
    size_t slen;

    if (fmt == NULL)
        return;

    /* Since the length is not determinable beforehand, a search is
       performed using the truncating "vsnprintf" call (to avoid buffer
       overflows) on increasing potential sizes for the output result. */

    if ((n = (int)(2 * strlen(fmt))) < START_VSNBUFF)
        n = START_VSNBUFF;

    buff = (char*)xmalloc((size_t)(n + 2));
    memset(buff, 0, (size_t)(n + 2));

    for (;;) {
        va_start(arglist, fmt);
        r = arg_vsnprintf(buff, (size_t)(n + 1), fmt, arglist);
        va_end(arglist);

        slen = strlen(buff);
        if (slen < (size_t)n)
            break;

        if (r > n)
            n = r;
        else
            n += n;

        xfree(buff);
        buff = (char*)xmalloc((size_t)(n + 2));
        memset(buff, 0, (size_t)(n + 2));
    }

    arg_dstr_cat(ds, buff);
    xfree(buff);
}

static void setup_append_buf(arg_dstr_t ds, int new_space) {
    int total_space;

    /*
     * Make the append buffer larger, if that's necessary, then copy the
     * data into the append buffer and make the append buffer the official
     * data.
     */
    if (ds->data != ds->append_data) {
        /*
         * If the buffer is too big, then free it up so we go back to a
         * smaller buffer. This avoids tying up memory forever after a large
         * operation.
         */
        if (ds->append_data_size > 500) {
            xfree(ds->append_data);
            ds->append_data = NULL;
            ds->append_data_size = 0;
        }
        ds->append_used = (int)strlen(ds->data);
    } else if (ds->data[ds->append_used] != 0) {
        /*
         * Most likely someone has modified a result created by
         * arg_dstr_cat et al. so that it has a different size. Just
         * recompute the size.
         */
        ds->append_used = (int)strlen(ds->data);
    }

    total_space = new_space + ds->append_used;
    if (total_space >= ds->append_data_size) {
        char* newbuf = NULL;

        if (total_space < 100) {
            total_space = 200;
        } else {
            total_space *= 2;
        }
        newbuf = (char*)xmalloc((unsigned)total_space);
        memset(newbuf, 0, (size_t)total_space);
        strncpy(newbuf, ds->data, (size_t)total_space); /* NOSONAR */
        assert(newbuf[total_space - 1] == '\0');
        if (ds->append_data != NULL) {
            xfree(ds->append_data);
        }

        ds->append_data = newbuf;
        ds->append_data_size = total_space;
    } else if (ds->data != ds->append_data && ds->append_data != NULL) {
        strncpy(ds->append_data, ds->data, (size_t)ds->append_data_size); /* NOSONAR */
        assert(ds->append_data[ds->append_data_size - 1] == '\0');
    }

    assert(ds->append_data != NULL);
    arg_dstr_free(ds);
    ds->data = ds->append_data;
}

void arg_dstr_free(arg_dstr_t ds) {
    if (ds->free_proc != NULL) {
        if (ds->free_proc == ARG_DSTR_DYNAMIC) {
            xfree(ds->data);
        } else {
            (*ds->free_proc)(ds->data);
        }
        ds->free_proc = NULL;
    }
}

void arg_dstr_reset(arg_dstr_t ds) {
    arg_dstr_free(ds);
    if ((ds->append_data != NULL) && (ds->append_data_size > 0)) {
        xfree(ds->append_data);
        ds->append_data = NULL;
        ds->append_data_size = 0;
    }

    ds->data = ds->sbuf;
    ds->sbuf[0] = 0;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
