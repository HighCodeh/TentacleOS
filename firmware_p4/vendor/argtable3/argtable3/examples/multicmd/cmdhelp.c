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

#include "cmdhelp.h"

#include "app.h"
#include "argtable3.h"

#include <stdlib.h>
#include <string.h>

typedef struct cmd_name {
    char name[ARG_CMD_NAME_LEN];
} cmd_name_t;

static int compare_str(const void* a, const void* b) {
    return strcmp(((const cmd_name_t*)a)->name, ((const cmd_name_t*)b)->name);
}

const char* cmdhelp_name() {
    static char name[] = "help";
    return name;
}

const char* cmdhelp_description() {
    static char description[] = "output usage information";
    return description;
}

int cmdhelp_proc(int argc, char* argv[], arg_dstr_t res, void* ctx) {
    arg_str_t* cmd = arg_str1(NULL, NULL, cmdhelp_name(), NULL);
    arg_str_t* cmd_name = arg_str0(NULL, NULL, "<command>", NULL);
    arg_lit_t* help = arg_lit0("h", "help", "output usage information");
    arg_end_t* end = arg_end(20);
    void* argtable[] = {cmd, cmd_name, help, end};

    int exitcode = APP_OK;
    if (arg_nullcheck(argtable) != 0) {
        fprintf(stderr, "failed to allocate argtable\n");
        exitcode = APP_ERR_OUTOFMEMORY;
        goto exit;
    }

    int nerrors = arg_parse(argc, argv, argtable);
    if (arg_make_syntax_err_help_msg(res, cmdhelp_name(), help->count, nerrors, argtable, end, &exitcode)) {
        goto exit;
    }

    /****************************************************************************/
    /* process the command                                                      */
    /****************************************************************************/

    if (cmd_name->count == 0) {
        arg_dstr_cat(res, "Usage:\n");
        arg_dstr_catf(res, "  %s <command> [options] [args]\n\n", APP_NAME);
        arg_dstr_cat(res, "Available commands:\n");

        int cmd_count = arg_cmd_count();
        cmd_name_t* acmd = (cmd_name_t*)malloc(cmd_count * sizeof(cmd_name_t));
        if (acmd == NULL) {
            fprintf(stderr, "failed to allocate memory for command names\n");
            exitcode = APP_ERR_OUTOFMEMORY;
            goto exit;
        }

        size_t max_cmd_name_length = 0;
        arg_cmd_itr_t itr = arg_cmd_itr_create();
        int i = 0;
        do {
            arg_cmd_info_t* cmd_info = arg_cmd_itr_value(itr);
            snprintf(acmd[i].name, sizeof(acmd[i].name), "%s", cmd_info->name);
            i++;

            if (strlen(cmd_info->name) > max_cmd_name_length) {
                max_cmd_name_length = strlen(cmd_info->name);
            }
        } while (arg_cmd_itr_advance(itr));
        arg_cmd_itr_destroy(itr);

        arg_mgsort(acmd, cmd_count, sizeof(cmd_name_t), 0, cmd_count - 1, compare_str);
        for (i = 0; i < cmd_count; i++) {
            arg_cmd_info_t* cmd_info = arg_cmd_info(acmd[i].name);
            arg_dstr_catf(res, "  %-*s  %s\n", max_cmd_name_length, cmd_info->name, cmd_info->description);
        }
        free(acmd);

        arg_dstr_catf(res, "\nType \"%s help <command>\" for help on a specific command.\n", APP_NAME);
    } else {
        if (arg_cmd_info(cmd_name->sval[0]) == NULL) {
            arg_dstr_catf(res, "Unknown command: %s\n", cmd_name->sval[0]);
            arg_make_get_help_msg(res);
            exitcode = APP_ERR_UNKNOWN_CMD;
            goto exit;
        }

        arg_cmd_info_t* cmd_info = arg_cmd_info(cmd_name->sval[0]);
        int tmp_argc = 3;
        char* tmp_argv[] = {APP_NAME, cmd_info->name, "--help"};
        cmd_info->proc(tmp_argc, tmp_argv, res, ctx);
    }

exit:
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return exitcode;
}
