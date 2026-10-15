/*******************************************************************************
 * This file is part of the argtable3 library.
 *
 * Copyright (C) 2016-2025 Tom G. Huang
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

#include "cmdversion.h"

#include "app.h"
#include "argtable3.h"

const char* cmdversion_name() {
    static char name[] = "version";
    return name;
}

const char* cmdversion_description() {
    static char description[] = "output the version";
    return description;
}

int cmdversion_proc(int argc, char* argv[], arg_dstr_t res, void* ctx) {
    arg_str_t* cmd = arg_str1(NULL, NULL, cmdversion_name(), NULL);
    arg_lit_t* help = arg_lit0("h", "help", "output usage information");
    arg_end_t* end = arg_end(20);
    void* argtable[] = {cmd, help, end};

    int exitcode = APP_OK;
    if (arg_nullcheck(argtable) != 0) {
        fprintf(stderr, "failed to allocate argtable\n");
        exitcode = APP_ERR_OUTOFMEMORY;
        goto exit;
    }

    int nerrors = arg_parse(argc, argv, argtable);
    if (arg_make_syntax_err_help_msg(res, cmdversion_name(), help->count, nerrors, argtable, end, &exitcode)) {
        goto exit;
    }

    /****************************************************************************/
    /* process the command                                                      */
    /****************************************************************************/

    arg_dstr_catf(res, "v%d.%d.%d.%s", APP_VER_MAJOR, APP_VER_MINOR, APP_VER_PATCH, APP_VER_TAG);

exit:
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return exitcode;
}
