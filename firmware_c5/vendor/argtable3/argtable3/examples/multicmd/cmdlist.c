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

#include "cmdlist.h"

#include "app.h"
#include "argtable3.h"

const char* cmdlist_name() {
    static char name[] = "list";
    return name;
}

const char* cmdlist_description() {
    static char description[] = "list all command-line options";
    return description;
}

int cmdlist_proc(int argc, char* argv[], arg_dstr_t res, void* ctx) {
    arg_str_t* cmd = arg_str1(NULL, NULL, cmdlist_name(), NULL);
    arg_int_t* intopt = arg_int0("i", "intopt", "<n>", "an integer option");
    arg_str_t* stropt = arg_str0("s", "stropt", "<s>", "a string option");
    arg_dbl_t* dblopt = arg_dbl0("d", "dblopt", "<f>", "a double option");
    arg_lit_t* boolopt = arg_lit0("b", "boolopt", "a boolean option");
    arg_file_t* fileopt = arg_file0("f", "fileopt", "<file>", "a file option");
    arg_lit_t* help = arg_lit0("h", "help", "output usage information");
    arg_end_t* end = arg_end(20);
    void* argtable[] = {cmd, intopt, stropt, dblopt, boolopt, fileopt, help, end};

    int exitcode = APP_OK;
    if (arg_nullcheck(argtable) != 0) {
        fprintf(stderr, "failed to allocate argtable\n");
        exitcode = APP_ERR_OUTOFMEMORY;
        goto exit;
    }

    intopt->ival[0] = 42;                  /* Default value for integer option */
    stropt->sval[0] = "default";           /* Default value for string option */
    dblopt->dval[0] = 3.14;                /* Default value for double option */
    fileopt->filename[0] = "default.txt";  /* Default value for file option */

    int nerrors = arg_parse(argc, argv, argtable);
    if (arg_make_syntax_err_help_msg(res, cmdlist_name(), help->count, nerrors, argtable, end, &exitcode)) {
        goto exit;
    }

    /****************************************************************************/
    /* process the command                                                      */
    /****************************************************************************/

    arg_dstr_catf(res, "intopt = %d\n", intopt->ival[0]);
    arg_dstr_catf(res, "stropt = %s\n", stropt->sval[0]);
    arg_dstr_catf(res, "dblopt = %.2f\n", dblopt->dval[0]);
    arg_dstr_catf(res, "boolopt = %s\n", boolopt->count > 0 ? "true" : "false");
    arg_dstr_catf(res, "fileopt = %s\n", fileopt->count > 0 ? fileopt->filename[0] : "no file");

exit:
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return exitcode;
}
