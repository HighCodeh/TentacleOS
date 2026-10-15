/*******************************************************************************
 * Example source code for using the argtable3 library to implement:
 *
 *    multicmd <command> [options] [args]
 *
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

#include "app.h"

#include "cmdhelp.h"
#include "cmdlist.h"
#include "cmdversion.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    arg_set_module_name(APP_NAME);
    arg_set_module_version(APP_VER_MAJOR, APP_VER_MINOR, APP_VER_PATCH, APP_VER_TAG);

    arg_cmd_init();
    arg_cmd_register(cmdhelp_name(), cmdhelp_proc, cmdhelp_description(), NULL);
    arg_cmd_register(cmdversion_name(), cmdversion_proc, cmdversion_description(), NULL);
    arg_cmd_register(cmdlist_name(), cmdlist_proc, cmdlist_description(), NULL);

    arg_dstr_t res = arg_dstr_create();
    if (argc == 1) {
        arg_make_get_help_msg(res);
        printf("%s", arg_dstr_cstr(res));
        arg_dstr_destroy(res);
        arg_cmd_uninit();
        return APP_OK;
    }

    int rv = arg_cmd_dispatch(argv[1], argc, argv, res);
    printf("%s\n", arg_dstr_cstr(res));
    arg_dstr_destroy(res);
    arg_cmd_uninit();
    return rv;
}

static char* m_app_errlist[] = {
        [APP_OK] = "No error",
        [APP_FAIL] = "Operation failed",
        [APP_ERR_OUTOFMEMORY] = "Not enough storage is available to process this command",
        [APP_ERR_UNKNOWN_CMD] = "Unknown command",
        [APP_LAST_ERROR_CODE] = "Unknown error",
};

static int m_app_nerr = sizeof(m_app_errlist) / sizeof(m_app_errlist[0]) - 1;
#define app_err_msg(m) m_app_errlist[(((m) < 0) || ((m) >= m_app_nerr) ? m_app_nerr : (m))]

const char* app_strerror(app_error_t code) {
    return app_err_msg(code);
}
