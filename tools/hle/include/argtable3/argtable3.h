#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arg_int { int count; int *ival; const char *shortopts; const char *longopts; const char *datatype; const char *glossary; } arg_int_t;
typedef struct arg_str { int count; const char **sval; const char *shortopts; const char *longopts; const char *datatype; const char *glossary; } arg_str_t;
typedef struct arg_lit { int count; const char *shortopts; const char *longopts; const char *glossary; } arg_lit_t;
typedef struct arg_file { int count; const char **filename; const char *shortopts; const char *longopts; const char *datatype; const char *glossary; } arg_file_t;
typedef struct arg_end { int count; const char *shortopts; const char *longopts; const char *datatype; const char *glossary; } arg_end_t;

arg_end_t *arg_end_n(int n);
arg_int_t *arg_int0(const char *s, const char *l, const char *d, const char *g);
arg_int_t *arg_int1(const char *s, const char *l, const char *d, const char *g);
arg_str_t *arg_str0(const char *s, const char *l, const char *d, const char *g);
arg_str_t *arg_str1(const char *s, const char *l, const char *d, const char *g);
arg_lit_t *arg_lit0(const char *s, const char *l, const char *g);
arg_file_t *arg_file0(const char *s, const char *l, const char *d, const char *g);
int arg_parse(int argc, char **argv, void **table);
void arg_freetable(void **table, int n);
void arg_print_errors(FILE *fp, const arg_end_t *end, const char *prog);

#define arg_end(n) arg_end_n(n)
#define arg_int0(s,l,d,g) arg_int0(s,l,d,g)
#define arg_int1(s,l,d,g) arg_int1(s,l,d,g)
#define arg_str0(s,l,d,g) arg_str0(s,l,d,g)
#define arg_str1(s,l,d,g) arg_str1(s,l,d,g)
#define arg_lit0(s,l,g) arg_lit0(s,l,g)
#define arg_file0(s,l,d,g) arg_file0(s,l,d,g)

#ifdef __cplusplus
}
#endif
