/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "argtable3/argtable3.h"

/* helper macros */
#define ARG_TABLE_FREE(tbl) arg_freetable((tbl), sizeof(tbl)/sizeof(tbl[0]))
#define PARSE_ARGS(tbl, argc, argv) arg_parse((int)(argc), (char**)(argv), (void**)tbl)

/*
===================== ARG TYPES ===================== */

TEST_CASE("argument constructors create valid structs", "[argtable3]")
{
    arg_rem_t *argRem = arg_rem(NULL, "comment");
    TEST_ASSERT_NOT_NULL(argRem);
    free(argRem);

    arg_lit_t *argLit0 = arg_lit0("v", "verbose", "Enable verbose");
    TEST_ASSERT_NOT_NULL(argLit0);
    free(argLit0);

    arg_lit_t *argLit1 = arg_lit1("f", "force", "Force operation");
    TEST_ASSERT_NOT_NULL(argLit1);
    free(argLit1);


    arg_int_t *argInt0 = arg_int0("i", "int", "<n>", "Optional int");
    TEST_ASSERT_NOT_NULL(argInt0);
    free(argInt0);

    arg_int_t *argInt1 = arg_int1("i", "int", "<n>", "Required int");
    TEST_ASSERT_NOT_NULL(argInt1);
    free(argInt1);

    arg_int_t *argIntn = arg_intn("i", "int", "<n>", 0, 3, "Multiple ints");
    TEST_ASSERT_NOT_NULL(argIntn);
    free(argIntn);

    arg_str_t *argStr0 = arg_str0("s", "str", "<str>", "Optional string");
    TEST_ASSERT_NOT_NULL(argStr0);
    free(argStr0);

    arg_str_t *argStr1 = arg_str1("s", "str", "<str>", "Required string");
    TEST_ASSERT_NOT_NULL(argStr1);
    free(argStr1);

    arg_str_t *argStrn = arg_strn("s", "str", "<str>", 1, 3, "Multi string");
    TEST_ASSERT_NOT_NULL(argStrn);
    free(argStrn);

    arg_file_t *argFile0 = arg_file0("f", "file", "<file>", "Optional file");
    TEST_ASSERT_NOT_NULL(argFile0);
    free(argFile0);

    arg_file_t *argFile1 = arg_file1("f", "file", "<file>", "Required file");
    TEST_ASSERT_NOT_NULL(argFile1);
    free(argFile1);

    arg_file_t *argFilen = arg_filen("f", "file", "<file>", 1, 3, "Multi file");
    TEST_ASSERT_NOT_NULL(argFilen);
    free(argFilen);

    arg_dbl_t *argDbl0 = arg_dbl0("d", "double", "<d>", "Optional double");
    TEST_ASSERT_NOT_NULL(argDbl0);
    free(argDbl0);

    arg_dbl_t *argDbl1 = arg_dbl1("d", "double", "<d>", "Required double");
    TEST_ASSERT_NOT_NULL(argDbl1);
    free(argDbl1);

    arg_dbl_t *argDbln = arg_dbln("d", "double", "<d>", 1, 2, "Multi double");
    TEST_ASSERT_NOT_NULL(argDbln);
    free(argDbln);

    arg_date_t *argDate0 = arg_date0("t", "time", "<date>", "%Y-%m-%d", "Optional date");
    TEST_ASSERT_NOT_NULL(argDate0);
    free(argDate0);

    arg_date_t *argDate1 = arg_date1("t", "time", "<date>", "%Y-%m-%d", "Required date");
    TEST_ASSERT_NOT_NULL(argDate1);
    free(argDate1);

    arg_rex_t *argRex0 = arg_rex0("r", "regex", "<expr>", "^[a-z]+$", 0, "Regex");
    TEST_ASSERT_NOT_NULL(argRex0);
    free(argRex0);

    arg_rex_t *argRex1 = arg_rex1("r", "regex", "<expr>", "^[a-z]+$", 0, "Regex");
    TEST_ASSERT_NOT_NULL(argRex1);
    free(argRex1);

    arg_end_t *argEnd = arg_end(5);
    TEST_ASSERT_NOT_NULL(argEnd);
    free(argEnd);

}

TEST_CASE("arg_int: parses optional integer", "[argtable3]")
{
    char *argv[] = {"prog", "-n", "100"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_int *n = arg_int0("n", "number", "<n>", "An integer value");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {n, end};

    TEST_ASSERT_EQUAL_INT(0, PARSE_ARGS(argtable, argc, argv));
    TEST_ASSERT_EQUAL_INT(100, n->ival[0]);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_dbl: parses optional double", "[argtable3]")
{
    char *argv[] = {"prog", "-d", "3.1415"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_dbl *d = arg_dbl0("d", "double", "<d>", "A double value");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {d, end};

    TEST_ASSERT_EQUAL_INT(0, PARSE_ARGS(argtable, argc, argv));
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 3.1415, d->dval[0]);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_lit: parses literal flags", "[argtable3]")
{
    char *argv[] = {"prog", "-v"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_lit *v = arg_lit0("v", "verbose", "Enable verbose output");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {v, end};

    TEST_ASSERT_EQUAL_INT(0, PARSE_ARGS(argtable, argc, argv));
    TEST_ASSERT_EQUAL_INT(1, v->count);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_str: parses string argument", "[argtable3]")
{
    char *argv[] = {"prog", "-s", "hello"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_str *s = arg_str0("s", "string", "<str>", "A string");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {s, end};

    TEST_ASSERT_EQUAL_INT(0, PARSE_ARGS(argtable, argc, argv));
    TEST_ASSERT_EQUAL_STRING("hello", s->sval[0]);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_file: parses file paths", "[argtable3]")
{
    char *argv[] = {"prog", "-f", "/tmp/test.txt"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_file *f = arg_file0("f", "file", "<file>", "A file path");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {f, end};

    TEST_ASSERT_EQUAL_INT(0, PARSE_ARGS(argtable, argc, argv));
    TEST_ASSERT_EQUAL_STRING("/tmp/test.txt", f->filename[0]);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_rex: validates regex input", "[argtable3]")
{
    char *argv[] = {"prog", "-r", "abc123"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_rex *r = arg_rex0("r", "regex", "[a-z]+[0-9]+", "<re>", 0, "Regex");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {r, end};

    TEST_ASSERT_EQUAL_INT(0, PARSE_ARGS(argtable, argc, argv));
    TEST_ASSERT_EQUAL_STRING("abc123", r->sval[0]);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_date: parses date-time string", "[argtable3]")
{
    char *argv[] = {"prog", "-t", "2025-06-27 12:00:00"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_date *dt = arg_date0("t", "time", "%Y-%m-%d %H:%M:%S", "<date>", "DateTime");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {dt, end};

    TEST_ASSERT_EQUAL_INT(0, PARSE_ARGS(argtable, argc, argv));
    TEST_ASSERT_EQUAL_INT(2025 - 1900, dt->tmval[0].tm_year);
    TEST_ASSERT_EQUAL_INT(5, dt->tmval[0].tm_mon);
    TEST_ASSERT_EQUAL_INT(27, dt->tmval[0].tm_mday);
    TEST_ASSERT_EQUAL_INT(12, dt->tmval[0].tm_hour);

    ARG_TABLE_FREE(argtable);
}

/* ===================== API Tests ===================== */

TEST_CASE("arg_print_syntax, glossary, and GNU glossary", "[argtable3]")
{
    struct arg_lit *verbose = arg_lit0("v", "verbose", "Enable verbose output");
    struct arg_str *name = arg_str1("n", "name", "<name>", "Name is required");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {verbose, name, end};

    char buf[256] = {0};
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT_NOT_NULL(f);

    arg_print_syntaxv(f, argtable, "\n");
    fflush(f);
    TEST_ASSERT_NOT_NULL(strstr(buf, "[-v|--verbose]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "-n|--name=<name>"));

    arg_print_glossary(f, argtable, "%s %s\n");
    fflush(f);
    TEST_ASSERT_NOT_NULL(strstr(buf, "-v, --verbose Enable verbose output"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "-n, --name=<name> Name is required"));
    fclose(f);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_print_errors prints expected message", "[argtable3]")
{
    char *argv[] = {"prog"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_int *num = arg_int1("n", NULL, "<num>", "Required number");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {num, end};

    int errors = arg_parse(argc, (char **)argv, argtable);
    TEST_ASSERT_GREATER_THAN(0, errors);

    char buf[256] = {0};
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT_NOT_NULL(f);
    arg_print_errors(f, end, argv[0]);
    fclose(f);
    printf("%s\n", buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "missing option"));

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_print_option and arg_print_option_ds output", "[argtable3]")
{
    char buf[128] = {0};
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT_NOT_NULL(f);
    arg_print_option(f, "f", "file", "<file>", "\n");
    fclose(f);
    TEST_ASSERT_NOT_NULL(strstr(buf, "-f|--file=<file>"));
}

TEST_CASE("returns errors for invalid input", "[argtable3]")
{
    char *argv[] = {"prog", "-i", "NaN"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_int *i = arg_int1("i", "int", "<n>", "An integer");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {i, end};

    int nerrors = PARSE_ARGS(argtable, argc, argv);
    TEST_ASSERT_GREATER_THAN(0, nerrors);

    char buf[128] = {0};
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT_NOT_NULL(f);
    arg_print_errors(f, end, argv[0]);
    fclose(f);
    TEST_ASSERT_NOT_NULL(strstr(buf, "invalid argument"));

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_parse, arg_nullcheck, arg_freetable basic flow", "[argtable3]")
{
    char *argv[] = {"prog", "-n", "123"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    struct arg_int *num = arg_int1("n", NULL, "<num>", "Required number");
    struct arg_end *end = arg_end(10);
    void *argtable[] = {num, end};

    TEST_ASSERT(0 == arg_nullcheck(argtable));
    TEST_ASSERT_EQUAL_INT(0, arg_parse(argc, (char **)argv, argtable));
    TEST_ASSERT_EQUAL_INT(123, num->ival[0]);

    ARG_TABLE_FREE(argtable);
}

TEST_CASE("arg_parse: success and error cases", "[argtable3]")
{
    // Success case
    char *argv_success[] = {"prog", "-n", "42", "--name", "ESP32"};
    int argc_success = sizeof(argv_success) / sizeof(argv_success[0]);

    struct arg_int *num = arg_int1("n", "number", "<n>", "A required number");
    struct arg_str *name = arg_str0(NULL, "name", "<name>", "An optional name");
    struct arg_end *end = arg_end(10);
    void *argtable_success[] = {num, name, end};

    int rc_success = arg_parse(argc_success, (char **)argv_success, argtable_success);
    TEST_ASSERT_EQUAL_INT(0, rc_success);
    TEST_ASSERT_EQUAL_INT(42, num->ival[0]);
    TEST_ASSERT_EQUAL_STRING("ESP32", name->sval[0]);

    ARG_TABLE_FREE(argtable_success);

    // Error case: missing required argument
    char *argv_fail[] = {"prog", "--name", "ESP32"};
    int argc_fail = sizeof(argv_fail) / sizeof(argv_fail[0]);

    num = arg_int1("n", "number", "<n>", "A required number");
    name = arg_str0(NULL, "name", "<name>", "An optional name");
    end = arg_end(10);
    void *argtable_fail[] = {num, name, end};

    int rc_fail = arg_parse(argc_fail, argv_fail, argtable_fail);
    TEST_ASSERT_GREATER_THAN(0, rc_fail);

    char buf[256] = {0};
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT_NOT_NULL(f);
    arg_print_errors(f, end, argv_fail[0]);
    fclose(f);
    TEST_ASSERT_NOT_NULL(strstr(buf, "missing option"));

    ARG_TABLE_FREE(argtable_fail);
}
