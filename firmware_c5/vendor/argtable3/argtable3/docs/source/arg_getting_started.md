# Getting Started

Argtable3 is a single-file ANSI-C library. All you have to do is adding
`argtable3.c` to your projects, and including `argtable3.h` in your source code.

For example, if you want to create a utility named util.exe that has the
following command-line options:

```shell
$ util.exe --help
Usage: util.exe [-v] [--help] [--version] [--level=<n>] [-o myfile] <file> [<file>]...
Demonstrate command-line parsing in argtable3.

  --help                    display this help and exit
  --version                 display version information and exit
  --level=<n>               foo value
  -v, --verbose             verbose output
  -o myfile                 output file
  <file>                    input files
```

You can implement the command-line parsing logic with Argtable3 in the following
way:

```c
#include "argtable3.h"

#include <stdio.h>
#include <stdlib.h>

/* global arg_xxx structs */
arg_lit_t *verb, *help, *version;
arg_int_t *level;
arg_file_t *o, *file;
arg_end_t *end;

int main(int argc, char *argv[])
{
    /* the global arg_xxx structs are initialised within the argtable */
    void *argtable[] = {
        help = arg_litn(NULL, "help", 0, 1, "display this help and exit"),
        version = arg_litn(NULL, "version", 0, 1, "display version info and exit"),
        level = arg_intn(NULL, "level", "<n>", 0, 1, "foo value"),
        verb = arg_litn("v", "verbose", 0, 1, "verbose output"),
        o = arg_filen("o", NULL, "myfile", 0, 1, "output file"),
        file = arg_filen(NULL, NULL, "<file>", 1, 100, "input files"),
        end = arg_end(20),
    };

    int exitcode = 0;
    char progname[] = "util.exe";

    int nerrors = arg_parse(argc, argv, argtable);

    /* special case: '--help' takes precedence over error reporting */
    if (help->count > 0) {
        printf("Usage: %s", progname);
        arg_print_syntax(stdout, argtable, "\n");
        printf("Demonstrate command-line parsing in argtable3.\n\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        exitcode = 0;
        goto exit;
    }

    /* If the parser returned any errors then display them and exit */
    if (nerrors > 0) {
        /* Display the error details contained in the arg_end struct.*/
        arg_print_errors(stdout, end, progname);
        printf("Try '%s --help' for more information.\n", progname);
        exitcode = 1;
        goto exit;
    }

exit:
    /* deallocate each non-null entry in argtable[] */
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return exitcode;
}
```

To build the program with Microsoft Visual C++, you can open the Visual Studio
Developer Command Prompt window, and type the following command:

```shell
C:\> cl.exe util.c argtable3.c
```

To build the program with GCC, MinGW, or Cygwin, you can open a shell window and
type the following command:

```shell
$ gcc util.c argtable3.c
```

If you can successfully build the program and execute `util.exe --help` to see
the help message, it means you’ve learned how to integrate Argtable3 into your
program.
