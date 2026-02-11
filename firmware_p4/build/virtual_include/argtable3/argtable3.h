/*******************************************************************************
 * argtable3: Declares the main interfaces of the library
 *
 * This file is part of the argtable3 library.
 *
 * Copyright (C) 1998-2001,2003-2011,2013 Stewart Heitmann
 * <sheitmann@users.sourceforge.net>
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

#ifndef ARGTABLE3
#define ARGTABLE3

#include <stdio.h> /* FILE */
#include <time.h>  /* struct tm */

#ifdef __cplusplus
extern "C" {
#endif

#define ARG_REX_ICASE 1

/* Maximum length of the command name */
#ifndef ARG_CMD_NAME_LEN
#define ARG_CMD_NAME_LEN 100
#endif /* ARG_CMD_NAME_LEN */

/* Maximum length of the command description */
#ifndef ARG_CMD_DESCRIPTION_LEN
#define ARG_CMD_DESCRIPTION_LEN 256
#endif /* ARG_CMD_DESCRIPTION_LEN */

/**
 * Error codes returned by argument parsing and validation.
 *
 * These error codes are used by Argtable3 to indicate specific problems
 * encountered during command-line parsing and validation. They are typically
 * stored in the `error` array of the `arg_end_t` struct and can be used to
 * generate detailed error messages for the user.
 */
enum {
    ARG_ELIMIT = 1, /**< Too many occurrences of an option or argument */
    ARG_EMALLOC,    /**< Memory allocation failure */
    ARG_ENOMATCH,   /**< Argument value does not match the expected format or pattern */
    ARG_ELONGOPT,   /**< Unknown or invalid long option encountered */
    ARG_EMISSARG    /**< Missing required argument value */
};

/**
 * Bit masks for the `flag` field in the `arg_hdr` struct.
 *
 * The `arg_hdr_flag` enum defines bitwise flags that describe special
 * properties of an argument entry in Argtable3. These flags are stored in the
 * `flag` field of the `arg_hdr` struct and can be combined using bitwise OR.
 *
 * These flags help Argtable3 functions determine how to parse and process each
 * argument entry.
 */
enum arg_hdr_flag {
    ARG_TERMINATOR = 0x1, /**< Marks the end of an argument table (sentinel entry) */
    ARG_HASVALUE = 0x2,   /**< Argument expects a value (e.g., `--output <file>`) */
    ARG_HASOPTVALUE = 0x4 /**< Argument can optionally take a value (e.g., `--color[=WHEN]`) */
};

#if defined(_WIN32)
#if defined(argtable3_EXPORTS)
#define ARG_EXTERN __declspec(dllexport)
#elif defined(argtable3_IMPORTS)
#define ARG_EXTERN __declspec(dllimport)
#else
#define ARG_EXTERN
#endif
#else
#define ARG_EXTERN
#endif

typedef struct _internal_arg_dstr* arg_dstr_t;
typedef void* arg_cmd_itr_t;

/**
 * Function pointer type for resetting an argument structure to its initial state.
 *
 * The `arg_resetfn` type defines the signature for functions that reset an
 * argument structure (such as `arg_lit_t`, `arg_int_t`, etc.) to its initial
 * state. This is typically used internally by Argtable3 to clear any parsed
 * values, counts, or state information before reusing the argument structure
 * for another round of parsing.
 *
 * This function pointer is primarily intended for developers who want to
 * implement and register new command-line option data types with Argtable3.
 * If you are extending Argtable3 with custom argument types, you should
 * provide a suitable reset function that matches this signature.
 *
 * The function receives a pointer to the parent argument structure and should
 * reset all relevant fields to their default values.
 *
 * Example usage:
 * ```
 * void my_arg_reset(void* parent) {
 *     arg_int_t* arg = (arg_int_t*)parent;
 *     arg->count = 0;
 *     // Reset other fields as needed...
 * }
 * ```
 *
 * @param parent Pointer to the argument structure to reset.
 */
typedef void(arg_resetfn)(void* parent);

/**
 * Function pointer type for scanning and parsing a command-line argument value.
 *
 * The `arg_scanfn` type defines the signature for functions that parse a
 * command-line argument value and store the result in the parent argument
 * structure (such as `arg_int_t`, `arg_str_t`, etc.). This function is called
 * by Argtable3 during argument parsing to convert the input string (`argval`)
 * into the appropriate data type and store it in the corresponding field of the
 * parent struct.
 *
 * This function pointer is primarily intended for developers who want to
 * implement and register new command-line option data types with Argtable3.
 * If you are extending Argtable3 with custom argument types, you should provide
 * a suitable scan function that matches this signature.
 *
 * @param parent Pointer to the argument structure where the parsed value should
 *               be stored.
 * @param argval The string value from the command line to be parsed.
 * @return Returns 0 on success, or a nonzero error code if parsing fails.
 */
typedef int(arg_scanfn)(void* parent, const char* argval);

/**
 * Function pointer type for validating a parsed argument structure.
 *
 * The `arg_checkfn` type defines the signature for functions that perform
 * post-parsing validation on an argument structure (such as `arg_int_t`,
 * `arg_str_t`, etc.). This function is called by Argtable3 after all arguments
 * have been parsed, allowing you to check for additional constraints or
 * perform custom validation logic.
 *
 * This function pointer is primarily intended for developers who want to
 * implement and register new command-line option data types with Argtable3.
 * If you are extending Argtable3 with custom argument types, you should provide
 * a suitable check function that matches this signature.
 *
 * @param parent Pointer to the argument structure to validate.
 * @return Returns 0 if validation succeeds, or a nonzero error code if validation
 *         fails.
 */
typedef int(arg_checkfn)(void* parent);

/**
 * Function pointer type for reporting argument parsing or validation errors.
 *
 * The `arg_errorfn` type defines the signature for functions that generate
 * error messages when an argument fails to parse or validate. This function is
 * called by Argtable3 when an error is detected for a specific argument, and it
 * is responsible for formatting and appending a descriptive error message to
 * the provided dynamic string object (`arg_dstr_t`).
 *
 * This function pointer is primarily intended for developers who want to
 * implement and register new command-line option data types with Argtable3.
 * If you are extending Argtable3 with custom argument types, you should provide
 * a suitable error reporting function that matches this signature.
 *
 * @param parent   Pointer to the argument structure that caused the error.
 * @param ds       Dynamic string object to which the error message should be
 *                 appended.
 * @param error    Error code indicating the type of error encountered.
 * @param argval   The offending argument value from the command line, or NULL if
 *                 not applicable.
 * @param progname The name of the program or command, used for context in the
 *                 error message.
 */
typedef void(arg_errorfn)(void* parent, arg_dstr_t ds, int error, const char* argval, const char* progname);

/**
 * Function pointer type for freeing a dynamically allocated string buffer.
 *
 * The `arg_dstr_freefn` type defines the signature for functions that release
 * memory allocated for a string buffer managed by a dynamic string object
 * (`arg_dstr_t`). This allows custom memory management strategies to be used
 * when setting or replacing the contents of a dynamic string.
 *
 * You can provide a custom free function when calling `arg_dstr_set`, or use
 * one of the standard macros (`ARG_DSTR_STATIC`, `ARG_DSTR_VOLATILE`,
 * `ARG_DSTR_DYNAMIC`) to control how the buffer is released.
 *
 * @param buf Pointer to the string buffer to be freed.
 *
 * @see arg_dstr_set, ARG_DSTR_STATIC, ARG_DSTR_VOLATILE, ARG_DSTR_DYNAMIC
 */
typedef void(arg_dstr_freefn)(char* buf);

/**
 * Function pointer type for sub-command handler functions.
 *
 * The `arg_cmdfn` type defines the signature for functions that implement the
 * logic of a sub-command in a multi-command command-line application. When a
 * sub-command is dispatched (for example, via `arg_cmd_dispatch`), the
 * corresponding handler function is called with the command-line arguments,
 * a dynamic string buffer for output or error messages, and an optional
 * user-defined context pointer.
 *
 * This function pointer is primarily intended for developers who want to
 * implement custom sub-commands and register them with Argtable3. The handler
 * should return 0 on success, or a nonzero error code on failure.
 *
 * @param argc The number of command-line arguments for the sub-command.
 * @param argv The array of command-line arguments for the sub-command.
 * @param res  Dynamic string buffer for output or error messages.
 * @param ctx  User-defined context pointer, as provided during registration.
 * @return 0 on success, or a nonzero error code on failure.
 */
typedef int (*arg_cmdfn)(int argc, char* argv[], arg_dstr_t res, void* ctx);

/**
 * Function pointer type for custom comparison functions used in sorting.
 *
 * The `arg_comparefn` type defines the signature for functions that compare two
 * elements, typically used with sorting algorithms such as `arg_mgsort`. The
 * comparison function should return an integer less than, equal to, or greater
 * than zero if the first argument is considered to be respectively less than,
 * equal to, or greater than the second.
 *
 * This function pointer is primarily intended for developers who want to
 * implement custom sorting or searching logic for arrays of arbitrary data
 * types within Argtable3 or their own applications.
 *
 * @param k1 Pointer to the first element to compare.
 * @param k2 Pointer to the second element to compare.
 * @return Negative value if `k1` < `k2`, zero if `k1` == `k2`, positive value
 *         if `k1` > `k2`.
 *
 * @see arg_mgsort
 */
typedef int(arg_comparefn)(const void* k1, const void* k2);

/**
 * Defines common properties shared by all `arg_<type>` structs.
 *
 * In the Argtable3 library, every `arg_<type>` struct must begin with an
 * `arg_hdr` struct as its first member. This allows Argtable3 functions to
 * access shared metadata about the command-line option, such as its option
 * tags, data type string, glossary text, and other attributes.
 *
 * The `arg_hdr` struct also contains pointers to type-specific functions
 * provided by each `arg_<type>` implementation. These functions handle tasks
 * such as parsing the option, performing post-parse validation, and reporting
 * errors. Although these function pointers are initialized by the constructor
 * of the respective `arg_<type>` struct, they can be modified by the user after
 * construction if necessary. However, they are intended to remain unchanged
 * once initialized.
 */
typedef struct arg_hdr {
    char flag;             /**< Modifier flags for this option (see `enum arg_hdr_flag`) */
    const char* shortopts; /**< String listing the short option characters (e.g., "hv") */
    const char* longopts;  /**< String listing the long option names, comma-separated (e.g., "verbose,debug") */
    const char* datatype;  /**< Description of the argument data type (e.g., "<file>") */
    const char* glossary;  /**< Description of the option as shown in the glossary/help output */
    int mincount;          /**< Minimum number of occurrences of this option accepted */
    int maxcount;          /**< Maximum number of occurrences of this option accepted */
    void* parent;          /**< Pointer to the parent arg_<type> struct instance */
    arg_resetfn* resetfn;  /**< Pointer to the type-specific reset function for this argument */
    arg_scanfn* scanfn;    /**< Pointer to the type-specific scan (parsing) function */
    arg_checkfn* checkfn;  /**< Pointer to the type-specific validation function */
    arg_errorfn* errorfn;  /**< Pointer to the type-specific error reporting function */
    void* priv;            /**< Pointer to private data for use by arg_<type> functions */
} arg_hdr_t;

/**
 * Structure for storing remarks or custom lines in the syntax or glossary
 * output.
 *
 * The `arg_rem` struct is used to add extra lines of text to the syntax or
 * glossary output generated by Argtable3. Unlike other argument types,
 * `arg_rem` does not correspond to a command-line argument and does not affect
 * argument parsing. Instead, it is a dummy entry that allows you to insert
 * remarks, explanations, or custom formatting into the help or usage messages.
 *
 * This is especially useful for providing additional context, grouping related
 * options, or clarifying the usage of certain arguments in the generated
 * documentation.
 *
 * Example usage:
 * ```
 * // Add extra lines to the glossary for the --update option
 * arg_lit_t *update  = arg_litn("u", "update", 0, 1, "copy only when SOURCE files are");
 * arg_rem_t *update1 = arg_rem(NULL,                 "  newer than destination files");
 * arg_rem_t *update2 = arg_rem(NULL,                 "  or when destination files");
 * arg_rem_t *update3 = arg_rem(NULL,                 "  are missing");
 * void *argtable[] = { update, update1, update2, update3, ... };
 *
 * // Add a data type entry for a positional argument in the syntax
 * arg_rem_t *dest = arg_rem("DEST|DIRECTORY", NULL);
 * void *argtable[] = { ..., dest, ... };
 * ```
 *
 * @see arg_rem()
 */
typedef struct arg_rem {
    struct arg_hdr hdr; /**< The mandatory argtable header struct */
} arg_rem_t;

/**
 * Structure for storing literal (boolean flag) argument information.
 *
 * The `arg_lit` struct is used to parse and store literal arguments (boolean
 * flags) from the command line. It is suitable for options that do not take a
 * value, such as `-h` for help or `--verbose` for enabling verbose output. Each
 * occurrence of the flag increases the `count` field, allowing you to detect
 * how many times the flag was specified.
 *
 * Example usage:
 * ```
 * // Accepts a help flag and a verbose flag (which can be specified multiple times)
 * arg_lit_t *help    = arg_lit0("h", "help", "Display help");
 * arg_lit_t *verbose = arg_litn("v", "verbose", 0, 3, "Increase verbosity");
 * arg_end_t *end     = arg_end(20);
 * void *argtable[] = { help, verbose, end };
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (help->count > 0) {
 *     printf("Help requested\n");
 * }
 * if (verbose->count > 0) {
 *     printf("Verbosity level: %d\n", verbose->count);
 * }
 * ```
 *
 * @see arg_lit0, arg_lit1, arg_litn
 */
typedef struct arg_lit {
    struct arg_hdr hdr; /**< The mandatory argtable header struct */
    int count;          /**< Number of times this flag appears on the command line */
} arg_lit_t;

/**
 * Structure for storing int-typed argument information.
 *
 * The `arg_int` struct is used to parse and store integer arguments from the
 * command line. It is suitable for options that accept numeric values without
 * fractional parts, such as counts, indices, or other whole-number parameters.
 *
 * The `count` field stores the number of successfully matched integer
 * arguments, and the `ival` array holds the parsed integer values as provided
 * by the user.
 *
 * Example usage:
 * ```
 * // Accepts one or more integer arguments
 * arg_int_t *numbers = arg_intn("n", "number", "<int>", 1, 5, "Input numbers");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {numbers, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && numbers->count > 0) {
 *     for (int i = 0; i < numbers->count; ++i) {
 *         printf("Input number: %d\n", numbers->ival[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @see arg_int0, arg_int1, arg_intn
 */
typedef struct arg_int {
    struct arg_hdr hdr; /**< The mandatory argtable header struct */
    int count;          /**< Number of times this argument appears on the command line */
    int* ival;          /**< Array of parsed integer argument values */
} arg_int_t;

/**
 * Structure for storing double-typed argument information.
 *
 * The `arg_dbl` struct is used to parse and store double-precision
 * floating-point arguments from the command line. It is suitable for options
 * that accept numeric values with fractional parts, such as thresholds, ratios,
 * or measurements.
 *
 * The `count` field stores the number of successfully matched double arguments,
 * and the `dval` array holds the parsed double values as provided by the user.
 *
 * Example usage:
 * ```
 * // Accepts one or more double arguments
 * arg_dbl_t *values = arg_dbln("v", "value", "<double>", 1, 5, "Input values");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {values, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && values->count > 0) {
 *     for (int i = 0; i < values->count; ++i) {
 *         printf("Input value: %f\n", values->dval[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @see arg_dbl0, arg_dbl1, arg_dbln
 */
typedef struct arg_dbl {
    struct arg_hdr hdr; /**< The mandatory argtable header struct */
    int count;          /**< Number of times this argument appears on the command line */
    double* dval;       /**< Array of parsed double argument values */
} arg_dbl_t;

/**
 * Structure for storing string-typed argument information.
 *
 * The `arg_str` struct is used to parse and store string arguments from the
 * command line. It is suitable for options that accept arbitrary text input,
 * such as file names, user names, or other string values.
 *
 * The `count` field stores the number of successfully matched string arguments,
 * and the `sval` array holds the parsed string values as provided by the user.
 *
 * Example usage:
 * ```
 * // Accepts one or more string arguments
 * arg_str_t *inputs = arg_strn(NULL, NULL, "<input>", 1, 10, "Input strings");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {inputs, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && inputs->count > 0) {
 *     for (int i = 0; i < inputs->count; ++i) {
 *         printf("Input string: %s\n", inputs->sval[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @see arg_str0, arg_str1, arg_strn
 */
typedef struct arg_str {
    struct arg_hdr hdr; /**< The mandatory argtable header struct */
    int count;          /**< Number of times this argument appears on the command line */
    const char** sval;  /**< Array of parsed string argument values */
} arg_str_t;

/**
 * Structure for storing regular expression-typed argument information.
 *
 * The `arg_rex` struct is used to parse and store command-line arguments that
 * must match a specified regular expression pattern. This allows applications
 * to validate input against complex patterns, such as email addresses,
 * identifiers, or custom formats.
 *
 * The `pattern` is specified when constructing the argument and is used to
 * check each input value. The `count` field stores the number of successfully
 * matched arguments, and the `sval` array holds the matched strings.
 *
 * Example usage:
 * ```
 * // Accepts one or more arguments matching a simple email pattern
 * arg_rex_t *emails = arg_rexn(NULL, "email", "^[^@]+@[^@]+\\.[^@]+$", "<email>", 1, 10, 0, "Email addresses");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {emails, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && emails->count > 0) {
 *     for (int i = 0; i < emails->count; ++i) {
 *         printf("Matched email: %s\n", emails->sval[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @see arg_rex0, arg_rex1, arg_rexn
 */
typedef struct arg_rex {
    struct arg_hdr hdr; /**< The mandatory argtable header struct */
    int count;          /**< Number of times this argument appears on the command line */
    const char** sval;  /**< Array of parsed string argument values */
} arg_rex_t;

/**
 * Structure for storing file-typed argument information.
 *
 * The `arg_file` struct is used to parse and store file path arguments from the
 * command line. It provides convenient access to the full filename, the
 * basename (file name without path), and the file extension for each matched
 * argument. This allows applications to easily process and validate
 * file-related options.
 *
 * The `count` field stores the number of successfully matched file arguments.
 * The `filename` array holds the full file paths as provided by the user, while
 * the `basename` and `extension` arrays provide the corresponding file names
 * and extensions, respectively.
 *
 * Example usage:
 * ```
 * // Accepts one or more file arguments
 * arg_file_t *files = arg_filen(NULL, NULL, "<file>", 1, 100, "Input files");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {files, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && files->count > 0) {
 *     for (int i = 0; i < files->count; ++i) {
 *         printf("File: %s, Basename: %s, Extension: %s\n",
 *             files->filename[i], files->basename[i], files->extension[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @see arg_file0, arg_file1, arg_filen
 */
typedef struct arg_file {
    struct arg_hdr hdr;     /**< The mandatory argtable header struct */
    int count;              /**< Number of times this argument appears on the command line */
    const char** filename;  /**< Array of parsed file path values (e.g., /home/foo.bar) */
    const char** basename;  /**< Array of parsed base names (e.g., foo.bar) */
    const char** extension; /**< Array of parsed file extensions (e.g., .bar) */
} arg_file_t;

/**
 * Structure for storing date-typed argument information.
 *
 * The `arg_date` struct is used to parse and store date or time arguments from
 * the command line. It supports flexible date/time formats, which are specified
 * using a `strptime`-style format string. Each successfully parsed date
 * argument is converted into a `struct tm` value and stored in the `tmval`
 * array.
 *
 * The `format` field defines the expected input format for date arguments,
 * allowing you to accept a wide range of date/time styles. The `count` field
 * stores the number of successfully matched arguments, and the `tmval` array
 * holds the parsed results.
 *
 * Example usage:
 * ```
 * // Accepts one required date argument in YYYY-MM-DD format
 * arg_date_t *date = arg_date1(NULL, "date", "%Y-%m-%d", "<date>", "Date in YYYY-MM-DD format");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {date, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && date->count > 0) {
 *     printf("Parsed date: %04d-%02d-%02d\n",
 *         date->tmval[0].tm_year + 1900, date->tmval[0].tm_mon + 1, date->tmval[0].tm_mday);
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @see arg_date0, arg_date1, arg_daten
 */
typedef struct arg_date {
    struct arg_hdr hdr; /**< The mandatory argtable header struct */
    const char* format; /**< strptime format string used to parse the date */
    int count;          /**< Number of times this argument appears on the command line */
    struct tm* tmval;   /**< Array of parsed time values */
} arg_date_t;

/**
 * Structure for collecting parser errors and terminating an argument table.
 *
 * The `arg_end` struct is used in Argtable3 to mark the end of an argument
 * table and to collect information about any errors encountered during
 * command-line parsing. It stores pointers to offending arguments in the input
 * array, allowing the application to report detailed error messages to the
 * user.
 *
 * Typically, an `arg_end_t` instance is created using the `arg_end` function
 * and placed as the last element in the argument table array. After parsing,
 * the structure contains information about missing required arguments, invalid
 * values, or other parsing errors.
 *
 * Example usage:
 * ```
 * arg_lit_t *help = arg_lit0("h", "help", "Display help");
 * arg_int_t *count = arg_int0("c", "count", "<n>", "Number of times");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {help, count, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors > 0) {
 *     arg_print_errors(stdout, end, argv[0]); // handle errors...
 * }
 * ```
 */
typedef struct arg_end {
    struct arg_hdr hdr;  /**< The mandatory argtable header struct */
    int count;           /**< Number of errors encountered */
    int* error;          /**< Array of error codes */
    void** parent;       /**< Array of pointers to offending arg_<type> struct */
    const char** argval; /**< Array of pointers to offending argv[] string */
} arg_end_t;

/**
 * Structure for storing sub-command information.
 *
 * The `arg_cmd_info` struct is used to represent metadata and handler
 * information for a sub-command in a multi-command command-line application.
 * Each sub-command can have its own name, description, handler function, and
 * context pointer, allowing you to build flexible interfaces similar to tools
 * like `git` or `docker`.
 *
 * The `name` field stores the sub-command name, while the `description`
 * provides a short summary for help and usage messages. The `proc` field is a
 * pointer to the function that implements the sub-command's behavior, and `ctx`
 * is a user-defined context pointer that can be used to pass additional data to
 * the handler.
 *
 * Example usage:
 * ```
 * // Define a handler function for the "list" sub-command
 * int list_cmd(int argc, char* argv[], arg_dstr_t res, void* ctx) {
 *     // Implementation for the "list" command
 *     return 0;
 * }
 *
 * // Register the sub-command
 * arg_cmd_register("list", list_cmd, "List all items", NULL);
 *
 * // Retrieve sub-command info
 * arg_cmd_info_t* info = arg_cmd_info("list");
 * if (info) {
 *     printf("Sub-command: %s - %s\n", info->name, info->description);
 * }
 * ```
 *
 * @see arg_cmd_register, arg_cmd_info, arg_cmd_dispatch
 */
typedef struct arg_cmd_info {
    char name[ARG_CMD_NAME_LEN];               /**< Sub-command name */
    char description[ARG_CMD_DESCRIPTION_LEN]; /**< Short description of the sub-command */
    arg_cmdfn proc;                            /**< Sub-command handler function */
    void* ctx;                                 /**< User-defined context pointer for the sub-command */
} arg_cmd_info_t;

/**** arg_<type> constructor functions *********************************/

/**
 * Adds a remark or custom line to the syntax or glossary output.
 *
 * The `arg_rem` function allows you to insert extra lines of text into the
 * syntax or glossary output generated by Argtable3. Instead of embedding
 * newline characters directly in your argument table strings—which can make
 * the code messy—you can add `arg_rem` structs to your argument table. These
 * are dummy entries: they do not affect argument parsing, but their `datatype`
 * and `glossary` strings are included in the output of `arg_print_syntax` and
 * `arg_print_glossary`.
 *
 * The name `arg_rem` stands for *remark*, inspired by the `REM` statement in
 * the BASIC programming language.
 *
 * For example, in the `mv` example program, we use `arg_rem` to add additional
 * lines for the `-u|--update` option in the glossary:
 * ```
 * arg_lit_t *update  = arg_litn("u", "update", 0, 1, "copy only when SOURCE files are");
 * arg_rem_t *update1 = arg_rem(NULL,                 "  newer than destination files");
 * arg_rem_t *update1 = arg_rem(NULL,                 "  or when destination files");
 * arg_rem_t *update2 = arg_rem(NULL,                 "  are missing");
 * ```
 *
 * which will make the glossay look like:
 * ```
 *   -u, --update                   copy only when SOURCE files are
 *                                    newer than destination files
 *                                    or when the destination files
 *                                    are missing
 * ```
 *
 * We also use `arg_rem` to add a data type entry for the ordinary argument in
 * the syntax:
 * ```
 * arg_rem_t *dest = arg_rem ("DEST|DIRECTORY", NULL);
 * ```
 *
 * which will make the syntax look like:
 * ```
 * $ mv --help
 * Usage: mv [-bfiuv] [--backup=[CONTROL]] [--reply={yes,no,query}]
 * [--strip-trailing-slashes] [-S SUFFIX] [--target-directory=DIRECTORY]
 * [--help] [--version] SOURCE [SOURCE]... DEST|DIRECTORY
 * ```
 *
 * @param datatype  The data type or positional argument string to display in the
 *                  syntax output, or NULL if not needed.
 * @param glossary  The remark or extra line to display in the glossary output,
 *                  or NULL if not needed.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_rem_t`. Otherwise,
 *   returns `NULL` if there is insufficient memory available.
 */
ARG_EXTERN arg_rem_t* arg_rem(const char* datatype, const char* glossary);

/**
 * Creates a literal (boolean flag) argument for the command-line parser.
 *
 * The `arg_litn` function defines an option that does not take a value, such as
 * a boolean flag (e.g., `-h` for help or `--verbose` for verbosity). You can
 * specify the minimum and maximum number of times the flag can appear, making
 * it suitable for optional, required, or repeatable flags. Each occurrence of
 * the flag increases the `count` field in the resulting `arg_lit_t` struct.
 *
 * A classic example is the `tar` utility, which uses `--verbose` or `-v` to
 * show the files being worked on as `tar` is creating an archive:
 * ```
 * $ tar -cvf afiles.tar apple angst aspic
 * apple
 * angst
 * aspic
 * ```
 *
 * Each occurrence of `--verbose` or `-v` on the command line increases the
 * verbosity level by one. Therefore, if you need more details on the output,
 * specify it twice:
 * ```
 * $ tar -cvvf afiles.tar apple angst aspic
 * -rw-r--r-- gray/staff    62373 2006-06-09 12:06 apple
 * -rw-r--r-- gray/staff    11481 2006-06-09 12:06 angst
 * -rw-r--r-- gray/staff    23152 2006-06-09 12:06 aspic
 * ```
 *
 * The `arg_litn` function allows you to specify both the minimum and maximum
 * number of times a flag can appear. For convenience and backward
 * compatibility, `arg_lit0` is provided as a helper for optional flags
 * (where `mincount = 0` and `maxcount = 1`), and `arg_lit1` is a helper for
 * required flags (where `mincount = 1` and `maxcount = 1`). While `arg_lit0`
 * and `arg_lit1` are available, it is recommended to use `arg_litn` in new
 * code as it is more explicit and flexible.
 *
 * Example usage:
 * ```
 * arg_lit_t *list    = arg_litn("lL",NULL,           0, 1, "list files");
 * arg_lit_t *verbose = arg_litn("v","verbose,debug", 0, 3, "verbosity level");
 * arg_lit_t *help    = arg_litn("h","help",          0, 1, "print this help");
 * arg_lit_t *version = arg_litn(NULL,"version",      0, 1, "print version info");
 * ```
 *
 * @param shortopts A string of single characters, each representing a short option
 *                  name (e.g., `"v"` for `-v`). Pass `NULL` if no short option is
 *                  desired.
 * @param longopts  A string of comma-separated long option names (e.g.,
 *                  `"verbose"` for `--verbose`). Pass `NULL` if no long option is
 *                  desired.
 * @param mincount  The minimum number of times the flag must appear (set to 0 for
 *                  optional).
 * @param maxcount  The maximum number of times the flag can appear (controls memory
 *                  allocation).
 * @param glossary  A short description of the argument for the glossary/help
 *                  output. Pass `NULL` to omit.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_lit_t`. Returns
 *   `NULL` if there is insufficient memory.
 */
ARG_EXTERN arg_lit_t* arg_litn(const char* shortopts, const char* longopts, int mincount, int maxcount, const char* glossary);
ARG_EXTERN arg_lit_t* arg_lit0(const char* shortopts, const char* longopts, const char* glossary);
ARG_EXTERN arg_lit_t* arg_lit1(const char* shortopts, const char* longopts, const char* glossary);

/**
 * Creates an integer argument for the command-line parser.
 *
 * The `arg_intn` function defines an option that accepts integer values from
 * the command line. You can specify the minimum and maximum number of times the
 * argument can appear, making it suitable for optional, required, or repeatable
 * integer options. Each occurrence of the option is parsed and stored in the
 * `ival` array of the resulting `arg_int_t` struct, allowing you to retrieve
 * all provided integer values after parsing.
 *
 * For convenience and backward compatibility, `arg_int0` is provided as a
 * helper for optional integer arguments (where `mincount = 0` and
 * `maxcount = 1`), and `arg_int1` is a helper for required integer arguments
 * (where `mincount = 1` and `maxcount = 1`). While `arg_int0` and `arg_int1`
 * are available, it is recommended to use `arg_intn` in new code as it is more
 * explicit and flexible.
 *
 * Example usage:
 * ```
 * // Accepts one or more integer arguments
 * arg_int_t *numbers = arg_intn("n", "number", "<int>", 1, 5, "Input numbers");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = { numbers, end };
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && numbers->count > 0) {
 *     for (int i = 0; i < numbers->count; ++i) {
 *         printf("Input number: %d\n", numbers->ival[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @param shortopts A string of single characters, each representing a short
 *                  option name (e.g., `"n"` for `-n`). Pass `NULL` if no short
 *                  option is desired.
 * @param longopts  A string of comma-separated long option names (e.g.,
 *                  `"number"` for `--number`). Pass `NULL` if no long option is
 *                  desired.
 * @param datatype  A string describing the expected data type (e.g., `"<int>"`),
 *                  shown in help messages.
 * @param mincount  The minimum number of times the argument must appear (set to 0
 *                  for optional).
 * @param maxcount  The maximum number of times the argument can appear (controls
 *                  memory allocation).
 * @param glossary  A short description of the argument for the glossary/help
 *                  output. Pass `NULL` to omit.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_int_t`. Returns
 *   `NULL` if there is insufficient memory.
 */
ARG_EXTERN arg_int_t* arg_intn(const char* shortopts, const char* longopts, const char* datatype, int mincount, int maxcount, const char* glossary);
ARG_EXTERN arg_int_t* arg_int0(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);
ARG_EXTERN arg_int_t* arg_int1(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);

/**
 * Creates a double-precision floating-point argument for the command-line parser.
 *
 * The `arg_dbln` function defines an option that accepts double-precision
 * floating-point values from the command line. You can specify the minimum and
 * maximum number of times the argument can appear, making it suitable for
 * optional, required, or repeatable floating-point options. Each occurrence of
 * the option is parsed and stored in the `dval` array of the resulting
 * `arg_dbl_t` struct, allowing you to retrieve all provided double values after
 * parsing.
 *
 * For convenience and backward compatibility, `arg_dbl0` is provided as a
 * helper for optional double arguments (where `mincount = 0` and
 * `maxcount = 1`), and `arg_dbl1` is a helper for required double arguments
 * (where `mincount = 1` and `maxcount = 1`). While `arg_dbl0` and `arg_dbl1`
 * are available, it is recommended to use `arg_dbln` in new code as it is more
 * explicit and flexible.
 *
 * Example usage:
 * ```
 * // Accepts one or more double arguments
 * arg_dbl_t *values = arg_dbln("v", "value", "<double>", 1, 5, "Input values");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {values, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && values->count > 0) {
 *     for (int i = 0; i < values->count; ++i) {
 *         printf("Input value: %f\n", values->dval[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @param shortopts A string of single characters, each representing a short
 *                  option name (e.g., `"v"` for `-v`). Pass `NULL` if no short
 *                  option is desired.
 * @param longopts  A string of comma-separated long option names (e.g.,
 *                  `"value"` for `--value`). Pass `NULL` if no long option is
 *                  desired.
 * @param datatype  A string describing the expected data type (e.g.,
 *                  `"<double>"`), shown in help messages.
 * @param mincount  The minimum number of times the argument must appear (set to
 *                  0 for optional).
 * @param maxcount  The maximum number of times the argument can appear
 *                  (controls memory allocation).
 * @param glossary  A short description of the argument for the glossary/help
 *                  output. Pass `NULL` to omit.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_dbl_t`. Returns
 *   `NULL` if there is insufficient memory.
 */
ARG_EXTERN arg_dbl_t* arg_dbln(const char* shortopts, const char* longopts, const char* datatype, int mincount, int maxcount, const char* glossary);
ARG_EXTERN arg_dbl_t* arg_dbl0(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);
ARG_EXTERN arg_dbl_t* arg_dbl1(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);

/**
 * Creates a string argument for the command-line parser.
 *
 * The `arg_strn` function defines an option that accepts string values from the
 * command line. You can specify the minimum and maximum number of times the
 * argument can appear, making it suitable for optional, required, or repeatable
 * string options. Each occurrence of the option is parsed and stored in the
 * `sval` array of the resulting `arg_str_t` struct, allowing you to retrieve
 * all provided string values after parsing.
 *
 * For convenience and backward compatibility, `arg_str0` is provided as a
 * helper for optional string arguments (where `mincount = 0` and
 * `maxcount = 1`), and `arg_str1` is a helper for required string arguments
 * (where `mincount = 1` and `maxcount = 1`). While `arg_str0` and `arg_str1`
 * are available, it is recommended to use `arg_strn` in new code as it is more
 * explicit and flexible.
 *
 * Example usage:
 * ```
 * // Accepts one or more string arguments
 * arg_str_t *inputs = arg_strn(NULL, NULL, "<input>", 1, 10, "Input strings");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = { inputs, end };
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && inputs->count > 0) {
 *     for (int i = 0; i < inputs->count; ++i) {
 *         printf("Input string: %s\n", inputs->sval[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @param shortopts A string of single characters, each representing a short
 *                  option name (e.g., `"i"` for `-i`). Pass `NULL` if no short
 *                  option is desired.
 * @param longopts  A string of comma-separated long option names (e.g.,
 *                  `"input"` for `--input`). Pass `NULL` if no long option is
 *                  desired.
 * @param datatype  A string describing the expected data type (e.g.,
 *                  `"<input>"`), shown in help messages.
 * @param mincount  The minimum number of times the argument must appear (set to
 *                  0 for optional).
 * @param maxcount  The maximum number of times the argument can appear
 *                  (controls memory allocation).
 * @param glossary  A short description of the argument for the glossary/help
 *                  output. Pass `NULL` to omit.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_str_t`. Returns
 *   `NULL` if there is insufficient memory.
 */
ARG_EXTERN arg_str_t* arg_strn(const char* shortopts, const char* longopts, const char* datatype, int mincount, int maxcount, const char* glossary);
ARG_EXTERN arg_str_t* arg_str0(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);
ARG_EXTERN arg_str_t* arg_str1(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);

/**
 * Creates a regular expression argument for the command-line parser.
 *
 * The `arg_rexn` function defines an option that accepts values matching a
 * specified regular expression pattern. You can specify the minimum and maximum
 * number of times the argument can appear, making it suitable for optional,
 * required, or repeatable regex-matched options. Each occurrence of the option
 * is parsed and stored in the `sval` array of the resulting `arg_rex_t` struct,
 * allowing you to retrieve all provided values that match the regular expression
 * after parsing.
 *
 * For convenience and backward compatibility, `arg_rex0` is provided as a
 * helper for optional regex arguments (where `mincount = 0` and `maxcount = 1`),
 * and `arg_rex1` is a helper for required regex arguments (where `mincount = 1`
 * and `maxcount = 1`). While `arg_rex0` and `arg_rex1` are available, it is
 * recommended to use `arg_rexn` in new code as it is more explicit and flexible.
 *
 * Example usage:
 * ```
 * // Accepts one or more arguments matching a simple email pattern
 * arg_rex_t *emails = arg_rexn(NULL, "email", "^[^@]+@[^@]+\\.[^@]+$", "<email>", 1, 10, 0, "Email addresses");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = { emails, end };
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && emails->count > 0) {
 *     for (int i = 0; i < emails->count; ++i) {
 *         printf("Matched email: %s\n", emails->sval[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @param shortopts A string of single characters, each representing a short
 *                  option name (e.g., `"e"` for `-e`). Pass `NULL` if no short
 *                  option is desired.
 * @param longopts  A string of comma-separated long option names (e.g.,
 *                  `"email"` for `--email`). Pass `NULL` if no long option is
 *                  desired.
 * @param pattern   The regular expression pattern to match input values.
 * @param datatype  A string describing the expected data type (e.g.,
 *                  `"<email>"`), shown in help messages.
 * @param mincount  The minimum number of times the argument must appear (set to
 *                  0 for optional).
 * @param maxcount  The maximum number of times the argument can appear
 *                  (controls memory allocation).
 * @param flags     Flags to modify regex matching behavior (e.g.,
 *                  `ARG_REX_ICASE` for case-insensitive).
 * @param glossary  A short description of the argument for the glossary/help
 *                  output. Pass `NULL` to omit.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_rex_t`. Returns
 *   `NULL` if there is insufficient memory.
 */
ARG_EXTERN arg_rex_t* arg_rexn(const char* shortopts,
                               const char* longopts,
                               const char* pattern,
                               const char* datatype,
                               int mincount,
                               int maxcount,
                               int flags,
                               const char* glossary);
ARG_EXTERN arg_rex_t* arg_rex0(const char* shortopts,
                               const char* longopts,
                               const char* pattern,
                               const char* datatype,
                               int flags,
                               const char* glossary);
ARG_EXTERN arg_rex_t* arg_rex1(const char* shortopts,
                               const char* longopts,
                               const char* pattern,
                               const char* datatype,
                               int flags,
                               const char* glossary);

/**
 * Creates a file path argument for the command-line parser.
 *
 * The `arg_filen` function defines an option that accepts file path values from
 * the command line. You can specify the minimum and maximum number of times the
 * argument can appear, making it suitable for optional, required, or repeatable
 * file arguments. Each occurrence of the option is parsed and stored in the
 * `filename` array of the resulting `arg_file_t` struct. The struct also
 * provides convenient access to the basename and extension for each file.
 *
 * For convenience and backward compatibility, `arg_file0` is provided as a
 * helper for optional file arguments (where `mincount = 0` and `maxcount = 1`),
 * and `arg_file1` is a helper for required file arguments (where `mincount = 1`
 * and `maxcount = 1`). While `arg_file0` and `arg_file1` are available, it is
 * recommended to use `arg_filen` in new code as it is more explicit and flexible.
 *
 * Example usage:
 * ```
 * // Accepts one or more file arguments
 * arg_file_t *files = arg_filen(NULL, NULL, "<file>", 1, 100, "Input files");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = { files, end };
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && files->count > 0) {
 *     for (int i = 0; i < files->count; ++i) {
 *         printf("File: %s, Basename: %s, Extension: %s\n",
 *             files->filename[i], files->basename[i], files->extension[i]);
 *     }
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @param shortopts A string of single characters, each representing a short
 *                  option name (e.g., `"f"` for `-f`). Pass `NULL` if no short
 *                  option is desired.
 * @param longopts  A string of comma-separated long option names (e.g.,
 *                  `"file"` for `--file`). Pass `NULL` if no long option is
 *                  desired.
 * @param datatype  A string describing the expected data type (e.g.,
 *                  `"<file>"`), shown in help messages.
 * @param mincount  The minimum number of times the argument must appear (set to
 *                  0 for optional).
 * @param maxcount  The maximum number of times the argument can appear
 *                  (controls memory allocation).
 * @param glossary  A short description of the argument for the glossary/help
 *                  output. Pass `NULL` to omit.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_file_t`. Returns
 *   `NULL` if there is insufficient memory.
 */
ARG_EXTERN arg_file_t* arg_filen(const char* shortopts, const char* longopts, const char* datatype, int mincount, int maxcount, const char* glossary);
ARG_EXTERN arg_file_t* arg_file0(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);
ARG_EXTERN arg_file_t* arg_file1(const char* shortopts, const char* longopts, const char* datatype, const char* glossary);

/**
 * Creates a date/time argument for the command-line parser.
 *
 * The `arg_daten` function defines an option that accepts date or time values
 * from the command line. You can specify the minimum and maximum number of
 * times the argument can appear, making it suitable for optional, required, or
 * repeatable date/time arguments. Each occurrence of the option is parsed using
 * the specified `format` (a `strptime`-style format string) and stored as a
 * `struct tm` value in the `tmval` array of the resulting `arg_date_t` struct,
 * allowing you to retrieve all provided date/time values after parsing.
 *
 * For convenience and backward compatibility, `arg_date0` is provided as a
 * helper for optional date arguments (where `mincount = 0` and `maxcount = 1`),
 * and `arg_date1` is a helper for required date arguments (where `mincount = 1`
 * and `maxcount = 1`). While `arg_date0` and `arg_date1` are available, it is
 * recommended to use `arg_daten` in new code as it is more explicit and flexible.
 *
 * Example usage:
 * ```
 * // Accepts one required date argument in YYYY-MM-DD format
 * arg_date_t *date = arg_date1(NULL, "date", "%Y-%m-%d", "<date>", "Date in YYYY-MM-DD format");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = { date, end };
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors == 0 && date->count > 0) {
 *     printf("Parsed date: %04d-%02d-%02d\n",
 *         date->tmval[0].tm_year + 1900, date->tmval[0].tm_mon + 1, date->tmval[0].tm_mday);
 * } else {
 *     arg_print_errors(stdout, end, argv[0]);
 * }
 * ```
 *
 * @param shortopts A string of single characters, each representing a short
 *                  option name (e.g., `"d"` for `-d`). Pass `NULL` if no short
 *                  option is desired.
 * @param longopts  A string of comma-separated long option names (e.g.,
 *                  `"date"` for `--date`). Pass `NULL` if no long option is
 *                  desired.
 * @param format    A `strptime`-style format string describing the expected
 *                  date/time input (e.g., `"%Y-%m-%d"`).
 * @param datatype  A string describing the expected data type (e.g.,
 *                  `"<date>"`), shown in help messages.
 * @param mincount  The minimum number of times the argument must appear (set to
 *                  0 for optional).
 * @param maxcount  The maximum number of times the argument can appear
 *                  (controls memory allocation).
 * @param glossary  A short description of the argument for the glossary/help
 *                  output. Pass `NULL` to omit.
 *
 * @return
 *   If successful, returns a pointer to the allocated `arg_date_t`. Returns
 *   `NULL` if there is insufficient memory.
 */
ARG_EXTERN arg_date_t*
arg_daten(const char* shortopts, const char* longopts, const char* format, const char* datatype, int mincount, int maxcount, const char* glossary);
ARG_EXTERN arg_date_t* arg_date0(const char* shortopts, const char* longopts, const char* format, const char* datatype, const char* glossary);
ARG_EXTERN arg_date_t* arg_date1(const char* shortopts, const char* longopts, const char* format, const char* datatype, const char* glossary);

/**
 * Creates an end-of-table marker and error collector for the argument table.
 *
 * The `arg_end` function is used to create an `arg_end_t` struct, which should
 * be placed as the last element in the argument table array. This structure
 * serves two purposes: it marks the end of the argument table for the parser,
 * and it collects information about any errors encountered during command-line
 * parsing.
 *
 * The `maxcount` parameter specifies the maximum number of errors that can be
 * recorded. After parsing, the `arg_end_t` struct contains details about
 * missing required arguments, invalid values, or other parsing errors, which
 * can be reported to the user using functions like `arg_print_errors`.
 *
 * Example usage:
 * ```
 * arg_lit_t *help = arg_lit0("h", "help", "Display help");
 * arg_int_t *count = arg_int0("c", "count", "<n>", "Number of times");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {help, count, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors > 0) {
 *     arg_print_errors(stdout, end, argv[0]);
 *     // handle errors...
 * }
 * ```
 *
 * @param maxcount The maximum number of errors to record during parsing.
 *                 Choose a value large enough to capture all possible errors.
 * @return
 *   If successful, returns a pointer to the allocated `arg_end_t`. Returns
 *   `NULL` if there is insufficient memory.
 *
 * @see arg_end_t, arg_parse, arg_print_errors
 */
ARG_EXTERN arg_end_t* arg_end(int maxcount);

/**** other functions *******************************************/

/**
 * Checks the argument table for null entries.
 *
 * The `arg_nullcheck` function scans the provided argument table array and
 * returns 1 if any entry is NULL, or 0 if all entries are valid. This is useful
 * for detecting memory allocation failures after constructing all argument
 * table entries with `arg_<type>` constructor functions, such as `arg_litn`.
 *
 * Instead of checking the return value of each constructor individually, you
 * can call `arg_nullcheck` once after building the argument table to ensure
 * that all arguments were allocated successfully. This helps make your code
 * cleaner and more robust.
 *
 * Example usage:
 * ```
 * arg_lit_t *list    = arg_lit0("lL",NULL,           "list files");
 * arg_lit_t *verbose = arg_lit0("v","verbose,debug", "verbose messages");
 * arg_lit_t *help    = arg_lit0(NULL,"help",         "print this help and exit");
 * arg_lit_t *version = arg_lit0(NULL,"version",      "print version and exit");
 * arg_end_t *end     = arg_end(20);
 * void *argtable[] = {list, verbose, help, version, end};
 * const char *progname = "myprog";
 * int exitcode = 0;
 *
 * if (arg_nullcheck(argtable) != 0) {
 *     printf("%s: insufficient memory\n", progname);
 *     exitcode = 1;
 *     goto exit;
 * }
 * ```
 *
 * @param argtable Array of pointers to argument table structs.
 *
 * @return Returns 1 if any entry is NULL, 0 if all entries are valid.
 */
ARG_EXTERN int arg_nullcheck(void** argtable);

/**
 * Parses the command-line arguments according to the specified argument table.
 *
 * The `arg_parse` function processes the command-line arguments provided in
 * `argv` and populates the fields of each argument structure in the `argtable`
 * array. It checks for the presence, validity, and value of each option or
 * positional argument as defined by the argument table. Any errors encountered
 * during parsing, such as missing required arguments or invalid values, are
 * recorded in the `arg_end_t` structure (typically the last entry in the table).
 *
 * After calling `arg_parse`, you can inspect the fields of each argument struct
 * (such as `count`, `ival`, `dval`, `sval`, etc.) to retrieve the parsed
 * values. If errors are detected (i.e., the return value is greater than zero),
 * you can use `arg_print_errors` to display detailed error messages to the user.
 *
 * Example usage:
 * ```
 * arg_lit_t *help = arg_lit0("h", "help", "Display help");
 * arg_int_t *count = arg_int0("c", "count", "<n>", "Number of times");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {help, count, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors > 0) {
 *     arg_print_errors(stdout, end, argv[0]);
 *     // handle errors...
 * }
 * ```
 *
 * @param argc     The number of command-line arguments passed to the program.
 *                 The value is always greater than or equal to 1.
 * @param argv     An array of null-terminated strings representing the
 *                 command-line arguments. By convention, `argv[0]` is the
 *                 program name, and `argv[1]` to `argv[argc-1]` are the
 *                 arguments. `argv[argc]` is always NULL.
 * @param argtable An array of pointers to argument table structs, each created
 *                 by an `arg_<type>` constructor. The last entry should be an
 *                 `arg_end` struct.
 *
 * @return The number of errors found during parsing. Returns 0 if parsing was
 *         successful and no errors were detected.
 */
ARG_EXTERN int arg_parse(int argc, char** argv, void** argtable);

/**
 * Prints a formatted command-line option specification to a file stream.
 *
 * The `arg_print_option` function generates a formatted representation of a
 * command-line option, including its short options, long options, data type,
 * and an optional suffix. This is useful for displaying option syntax in help
 * messages, usage output, or documentation.
 *
 * The formatted option is written to the specified file stream (`fp`). You can
 * control the appearance of the option specification, making it easy to
 * integrate with custom help or documentation systems.
 *
 * Example usage:
 * ```
 * // Print option to stdout
 * arg_print_option(stdout, "h", "help", NULL, NULL);
 * ```
 *
 * @param fp        Output file stream to write to (e.g., `stdout` or `stderr`).
 * @param shortopts String of short option characters (e.g., `"h"` for `-h`).
 * @param longopts  String of long option names, comma-separated (e.g., `"help"` for `--help`).
 * @param datatype  String describing the expected data type (e.g., `"<file>"`).
 * @param suffix    Optional string to append after the option specification.
 */
ARG_EXTERN void arg_print_option(FILE* fp, const char* shortopts, const char* longopts, const char* datatype, const char* suffix);

/**
 * Prints a formatted command-line option specification to a dynamic string.
 *
 * The `arg_print_option_ds` function generates a formatted representation of a
 * command-line option, including its short options, long options, data type,
 * and an optional suffix. This is useful for displaying option syntax in help
 * messages, usage output, or documentation that is built using dynamic string
 * buffers.
 *
 * The formatted option is written to the specified dynamic string object
 * (`arg_dstr_t`). You can control the appearance of the option specification,
 * making it easy to integrate with custom help or documentation systems that
 * require string-based output.
 *
 * Example usage:
 * ```
 * // Print option to a dynamic string
 * arg_dstr_t ds = arg_dstr_create();
 * arg_print_option_ds(ds, "v", "verbose", "<level>", NULL);
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds        Dynamic string object to write to.
 * @param shortopts String of short option characters (e.g., `"h"` for `-h`).
 * @param longopts  String of long option names, comma-separated (e.g., `"help"` for `--help`).
 * @param datatype  String describing the expected data type (e.g., `"<file>"`).
 * @param suffix    Optional string to append after the option specification.
 */
ARG_EXTERN void arg_print_option_ds(arg_dstr_t ds, const char* shortopts, const char* longopts, const char* datatype, const char* suffix);

/**
 * Prints a compact, single-line command-line syntax summary to a file stream.
 *
 * The `arg_print_syntax` function generates a concise, single-line usage
 * summary for the command-line options and arguments defined in the argument
 * table. This summary is useful for displaying quick usage information to
 * users, such as in the output of a `--help` or usage message.
 *
 * The formatted syntax summary is written to the specified file stream (`fp`).
 * You can append a custom `suffix` string to the end of the summary, for
 * example, to indicate positional arguments.
 *
 * Example usage:
 * ```
 * // Print compact syntax summary to stdout
 * arg_print_syntax(stdout, argtable, "[FILES...]");
 * ```
 *
 * @param fp      Output file stream to write to (e.g., `stdout` or `stderr`).
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 * @param suffix  String to append at the end of the syntax summary (e.g., for
 *                positional arguments).
 */
ARG_EXTERN void arg_print_syntax(FILE* fp, void** argtable, const char* suffix);

/**
 * Prints a compact, single-line command-line syntax summary to a dynamic string.
 *
 * The `arg_print_syntax_ds` function generates a concise, single-line usage
 * summary for the command-line options and arguments defined in the argument
 * table. This summary is useful for displaying quick usage information to
 * users, such as in the output of a `--help` or usage message, and is written
 * to a dynamic string object (`arg_dstr_t`).
 *
 * You can append a custom `suffix` string to the end of the summary, for
 * example, to indicate positional arguments.
 *
 * Example usage:
 * ```
 * // Print compact syntax summary to a dynamic string
 * arg_dstr_t ds = arg_dstr_create();
 * arg_print_syntax_ds(ds, argtable, "[FILES...]");
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds      Dynamic string object to write to.
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 * @param suffix  String to append at the end of the syntax summary (e.g., for
 *                positional arguments).
 */
ARG_EXTERN void arg_print_syntax_ds(arg_dstr_t ds, void** argtable, const char* suffix);

/**
 * Prints a verbose, multi-line command-line syntax summary to a file stream.
 *
 * The `arg_print_syntaxv` function generates a detailed, multi-line usage
 * summary for the command-line options and arguments defined in the argument
 * table. This verbose style provides more clarity than the compact single-line
 * form, making it easier for users to understand complex command-line
 * interfaces.
 *
 * The formatted syntax summary is written to the specified file stream (`fp`).
 * You can append a custom `suffix` string to the end of the summary, for
 * example, to indicate positional arguments.
 *
 * Example usage:
 * ```
 * // Print verbose syntax summary to stdout
 * arg_print_syntaxv(stdout, argtable, "[FILES...]");
 * ```
 *
 * @param fp      Output file stream to write to (e.g., `stdout` or `stderr`).
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 * @param suffix  String to append at the end of the syntax summary (e.g., for
 *                positional arguments).
 */
ARG_EXTERN void arg_print_syntaxv(FILE* fp, void** argtable, const char* suffix);

/**
 * Prints a verbose, multi-line command-line syntax summary to a dynamic string.
 *
 * The `arg_print_syntaxv_ds` function generates a detailed, multi-line usage
 * summary for the command-line options and arguments defined in the argument
 * table. This verbose style provides more clarity than the compact single-line
 * form, making it easier for users to understand complex command-line
 * interfaces.
 *
 * The formatted syntax summary is written to the specified dynamic string
 * object (`arg_dstr_t`). You can append a custom `suffix` string to the end of
 * the summary, for example, to indicate positional arguments.
 *
 * Example usage:
 * ```
 * // Print verbose syntax summary to a dynamic string
 * arg_dstr_t ds = arg_dstr_create();
 * arg_print_syntaxv_ds(ds, argtable, "[FILES...]");
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds      Dynamic string object to write to.
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 * @param suffix  String to append at the end of the syntax summary (e.g., for
 *                positional arguments).
 */
ARG_EXTERN void arg_print_syntaxv_ds(arg_dstr_t ds, void** argtable, const char* suffix);

/**
 * Prints the argument glossary in a customizable format to a file stream.
 *
 * The `arg_print_glossary` function generates a glossary of command-line
 * options and arguments, formatted according to the specified format string.
 * This glossary provides users with a summary of available options, their data
 * types, and descriptions, making it easier to understand the command-line
 * interface.
 *
 * The formatted glossary is written to the specified file stream (`fp`). The
 * format string allows you to control the layout and appearance of each
 * glossary entry, enabling integration with custom help or documentation
 * systems.
 *
 * Example usage:
 * ```
 * // Print glossary to stdout with a custom format
 * arg_print_glossary(stdout, argtable, "  %-20s %s\n");
 * ```
 *
 * @param fp      Output file stream to write to (e.g., `stdout` or `stderr`).
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 * @param format  Format string controlling the layout of each glossary entry.
 */
ARG_EXTERN void arg_print_glossary(FILE* fp, void** argtable, const char* format);

/**
 * Prints the argument glossary in a customizable format to a dynamic string.
 *
 * The `arg_print_glossary_ds` function generates a glossary of command-line
 * options and arguments, formatted according to the specified format string.
 * This glossary provides users with a summary of available options, their data
 * types, and descriptions, making it easier to understand the command-line
 * interface.
 *
 * The formatted glossary is written to the specified dynamic string object
 * (`arg_dstr_t`). The format string allows you to control the layout and
 * appearance of each glossary entry, enabling integration with custom help or
 * documentation systems.
 *
 * Example usage:
 * ```
 * // Print glossary to a dynamic string with a custom format
 * arg_dstr_t ds = arg_dstr_create();
 * arg_print_glossary_ds(ds, argtable, "  %-20s %s\n");
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds      Dynamic string object to write to.
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 * @param format  Format string controlling the layout of each glossary entry.
 */
ARG_EXTERN void arg_print_glossary_ds(arg_dstr_t ds, void** argtable, const char* format);

/**
 * Prints the argument glossary using strict GNU formatting conventions to a file
 * stream.
 *
 * The `arg_print_glossary_gnu` function generates a glossary of command-line
 * options and arguments, formatted to comply with GNU style conventions. In this
 * format, long options are vertically aligned in a second column, and lines are
 * wrapped at 80 characters for improved readability and consistency with GNU
 * command-line tool documentation.
 *
 * The formatted glossary is written to the specified file stream (`fp`). This
 * function is useful for generating help output that matches the look and feel
 * of standard GNU utilities.
 *
 * Example usage:
 * ```
 * // Print GNU-style glossary to stdout
 * arg_print_glossary_gnu(stdout, argtable);
 * ```
 *
 * @param fp      Output file stream to write to (e.g., `stdout` or `stderr`).
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 */
ARG_EXTERN void arg_print_glossary_gnu(FILE* fp, void** argtable);

/**
 * Prints the argument glossary using strict GNU formatting conventions to a
 * dynamic string.
 *
 * The `arg_print_glossary_gnu_ds` function generates a glossary of command-line
 * options and arguments, formatted to comply with GNU style conventions. In
 * this format, long options are vertically aligned in a second column, and
 * lines are wrapped at 80 characters for improved readability and consistency
 * with GNU command-line tool documentation.
 *
 * The formatted glossary is written to the specified dynamic string object
 * (`arg_dstr_t`). This function is useful for generating help output that
 * matches the look and feel of standard GNU utilities, especially when you need
 * the output as a string for further processing, logging, or GUI display.
 *
 * Example usage:
 * ```
 * // Print GNU-style glossary to a dynamic string
 * arg_dstr_t ds = arg_dstr_create();
 * arg_print_glossary_gnu_ds(ds, argtable);
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds      Dynamic string object to write to.
 * @param argtable Array of argument table structs describing the available
 *                 options and arguments.
 */
ARG_EXTERN void arg_print_glossary_gnu_ds(arg_dstr_t ds, void** argtable);

/**
 * Prints the details of all errors stored in the end data structure.
 *
 * The `arg_print_errors` function writes formatted error messages for all
 * errors recorded in the specified `arg_end_t` structure to the given file
 * stream (`fp`). The `progname` string is prepended to each error message,
 * making it suitable for displaying or logging error output in command-line
 * applications.
 *
 * This function is useful for reporting parsing errors to the user after
 * calling `arg_parse`. It provides clear feedback about missing required
 * arguments, invalid values, or other issues encountered during command-line
 * parsing.
 *
 * Example usage:
 * ```
 * arg_lit_t *help = arg_lit0("h", "help", "Display help");
 * arg_int_t *count = arg_int0("c", "count", "<n>", "Number of times");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = {help, count, end};
 *
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors > 0) {
 *     arg_print_errors(stderr, end, argv[0]);
 * }
 * ```
 *
 * @param fp       Output file stream to write to (e.g., `stdout` or `stderr`).
 * @param end      Pointer to the `arg_end` structure containing error details.
 * @param progname The name of the program to prepend to each error message.
 *
 * @see arg_parse, arg_print_errors_ds, arg_dstr_create, arg_dstr_cstr
 */
ARG_EXTERN void arg_print_errors(FILE* fp, arg_end_t* end, const char* progname);

/**
 * Prints the details of all errors stored in the end data structure to a dynamic
 * string.
 *
 * The `arg_print_errors_ds` function writes formatted error messages for all
 * errors recorded in the specified `arg_end_t` structure to the provided
 * dynamic string object (`arg_dstr_t`). The `progname` string is prepended to
 * each error message, making it suitable for displaying or logging error output
 * in applications that use dynamic string buffers instead of standard output
 * streams.
 *
 * This function is useful for applications that want to capture error messages
 * for later display, logging, or integration with GUI or web interfaces.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors > 0) {
 *     arg_print_errors_ds(ds, end, argv[0]);
 *     fprintf(stderr, "%s", arg_dstr_cstr(ds));
 * }
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds       Pointer to the dynamic string object to which the error
 *                 messages are written.
 * @param end      Pointer to the `arg_end` structure containing error details.
 * @param progname The name of the program to prepend to each error message.
 *
 * @see arg_print_errors, arg_parse, arg_dstr_create, arg_dstr_cstr
 */
ARG_EXTERN void arg_print_errors_ds(arg_dstr_t ds, arg_end_t* end, const char* progname);

/**
 * Prints a formatted block of text with specified left and right margins.
 *
 * The `arg_print_formatted` function outputs the given text to the specified
 * file stream (`fp`), arranging it in a column with the provided left and right
 * margins. This is useful for generating neatly aligned help messages,
 * glossaries, or any output where column formatting is desired.
 *
 * The function automatically wraps lines as needed to ensure that text does not
 * exceed the specified right margin, and indents each line according to the
 * left margin.
 *
 * Example usage:
 * ```
 * const char* msg = "This is a long help message that will be wrapped and "
 *                   "indented according to the specified margins.";
 * arg_print_formatted(stdout, 4, 60, msg);
 * ```
 *
 * @param fp      Output file stream to write to (e.g., `stdout` or `stderr`).
 * @param lmargin Left margin (number of spaces to indent each line).
 * @param rmargin Right margin (maximum line width).
 * @param text    Text to be printed and formatted.
 */
ARG_EXTERN void arg_print_formatted(FILE* fp, const unsigned lmargin, const unsigned rmargin, const char* text);

/**
 * Deallocates or frees all non-null entries in the argument table.
 *
 * The `arg_freetable` function iterates over the specified argument table array
 * and frees any non-null entries, releasing the memory allocated for each
 * argument structure. This is useful for cleaning up all argument objects
 * created by `arg_<type>` constructor functions after you are done parsing and
 * processing command-line arguments.
 *
 * You should call this function once for each argument table array before your
 * program exits to prevent memory leaks.
 *
 * Example usage:
 * ```
 * arg_lit_t *help = arg_lit0("h", "help", "Display help");
 * arg_int_t *count = arg_int0("c", "count", "<n>", "Number of times");
 * arg_end_t *end = arg_end(20);
 * void *argtable[] = { help, count, end };
 *
 * // ... use argtable ...
 *
 * arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
 * ```
 *
 * @param argtable An array of pointers to argument table structs.
 * @param n        The number of structs in the argument table.
 *
 * @see arg_free
 */
ARG_EXTERN void arg_freetable(void** argtable, size_t n);

/**
 * Creates a new dynamic string object.
 *
 * The `arg_dstr_create` function allocates and initializes a new dynamic string
 * object (`arg_dstr_t`). This object can be used to efficiently build, modify,
 * and manage strings of arbitrary length, such as help messages, error
 * messages, or logs.
 *
 * The dynamic string automatically grows as needed to accommodate additional
 * content appended via functions like `arg_dstr_cat`, `arg_dstr_catc`, or
 * `arg_dstr_catf`. When you are finished using the dynamic string, release its
 * resources with `arg_dstr_destroy` or `arg_dstr_free`.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_cat(ds, "Hello, ");
 * arg_dstr_cat(ds, "world!");
 * printf("%s\n", arg_dstr_cstr(ds)); // Output: Hello, world!
 * arg_dstr_destroy(ds);
 * ```
 *
 * @return A handle to the newly created dynamic string object, or `NULL` if
 *         allocation fails.
 *
 * @see arg_dstr_destroy, arg_dstr_cat, arg_dstr_catc, arg_dstr_catf,
 *      arg_dstr_cstr
 */
ARG_EXTERN arg_dstr_t arg_dstr_create(void);

/**
 * Destroys a dynamic string object and releases its resources.
 *
 * The `arg_dstr_destroy` function frees all memory and resources associated
 * with the specified dynamic string object (`arg_dstr_t`). After calling this
 * function, the dynamic string handle becomes invalid and must not be used
 * unless reinitialized with `arg_dstr_create`.
 *
 * This function should be called when you are finished using a dynamic string
 * to prevent memory leaks and ensure proper cleanup.
 *
 * The difference between `arg_dstr_destroy` and `arg_dstr_free` is that
 * `arg_dstr_destroy` both frees the memory and invalidates the handle, making
 * it safe for typical use cases where you want to fully release the dynamic
 * string. `arg_dstr_free` only releases the memory, but does not necessarily
 * invalidate the handle, and is intended for advanced or custom memory
 * management scenarios. For most applications, `arg_dstr_destroy` is the
 * preferred function to use.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_cat(ds, "Temporary string");
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds Pointer to the dynamic string object to destroy.
 *
 * @see arg_dstr_create, arg_dstr_free, arg_dstr_reset, arg_dstr_set
 */
ARG_EXTERN void arg_dstr_destroy(arg_dstr_t ds);

/**
 * Resets the dynamic string object to an empty state.
 *
 * The `arg_dstr_reset` function clears the contents of the specified dynamic
 * string object (`arg_dstr_t`), making it an empty string but retaining the
 * allocated memory for future use. This is useful when you want to reuse a
 * dynamic string buffer without reallocating memory, such as when building
 * multiple messages in a loop.
 *
 * Any previously stored string data is discarded, and the dynamic string is
 * ready to accept new content via `arg_dstr_cat`, `arg_dstr_catc`, or
 * `arg_dstr_catf`.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_cat(ds, "First message");
 * printf("%s\n", arg_dstr_cstr(ds)); // Output: First message
 * arg_dstr_reset(ds);
 * arg_dstr_cat(ds, "Second message");
 * printf("%s\n", arg_dstr_cstr(ds)); // Output: Second message
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds Pointer to the dynamic string object to reset.
 *
 * @see arg_dstr_create, arg_dstr_cat, arg_dstr_catc, arg_dstr_catf,
 *      arg_dstr_cstr, arg_dstr_destroy
 */
ARG_EXTERN void arg_dstr_reset(arg_dstr_t ds);

/**
 * Frees the memory associated with a dynamic string object.
 *
 * The `arg_dstr_free` function releases all memory and resources allocated for
 * the specified dynamic string object (`arg_dstr_t`). After calling this
 * function, the dynamic string handle becomes invalid and must not be used
 * unless reinitialized.
 *
 * The difference between `arg_dstr_free` and `arg_dstr_destroy` is that
 * `arg_dstr_free` only releases the memory, but does not necessarily invalidate
 * the handle. This is intended for advanced or custom memory management
 * scenarios. In contrast, `arg_dstr_destroy` both frees the memory and
 * invalidates the handle, making it safer and preferred for typical use cases
 * where you want to fully release the dynamic string.
 *
 * For most applications, use `arg_dstr_destroy` to ensure proper cleanup and
 * avoid accidental use of an invalid handle.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_cat(ds, "Temporary string");
 * arg_dstr_free(ds); // Frees the memory associated with ds
 * ```
 *
 * @param ds Pointer to the dynamic string object to free.
 *
 * @see arg_dstr_create, arg_dstr_destroy, arg_dstr_reset, arg_dstr_set
 */
ARG_EXTERN void arg_dstr_free(arg_dstr_t ds);

#define ARG_DSTR_STATIC ((arg_dstr_freefn*)0)
#define ARG_DSTR_VOLATILE ((arg_dstr_freefn*)1)
#define ARG_DSTR_DYNAMIC ((arg_dstr_freefn*)3)

/**
 * Sets the contents of the dynamic string object to a specified string.
 *
 * The `arg_dstr_set` function replaces the current contents of the dynamic
 * string object (`arg_dstr_t`) with the provided string. You can also specify a
 * custom free function to control how the memory for the string is managed.
 * This is useful when you want to take ownership of an existing string buffer
 * or integrate with custom memory management schemes.
 *
 * The `free_proc` parameter determines how the memory for the string will be
 * released when the dynamic string is reset or destroyed. You can use one of
 * the following standard macros:
 *
 * - `ARG_DSTR_STATIC`: Indicates that the string is statically allocated or
 *   should not be freed. Use this when the string is a string literal or
 *   managed elsewhere and does not need to be freed by Argtable3.
 * - `ARG_DSTR_VOLATILE`: Indicates that the string is temporary and should not
 *   be freed. Use this when the string is only valid for the current scope or
 *   will be replaced soon, and you do not want Argtable3 to free it.
 * - `ARG_DSTR_DYNAMIC`: Indicates that the string was dynamically allocated
 *   (e.g., with `malloc` or `strdup`) and should be freed by Argtable3. Use
 *   this when you want Argtable3 to take ownership and automatically free the
 *   memory when the dynamic string is reset or destroyed.
 *
 * If you provide a custom free function, it will be called to release the
 * string when appropriate.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * char* buf = strdup("Hello, Argtable!");
 * arg_dstr_set(ds, buf, ARG_DSTR_DYNAMIC); // ds takes ownership of buf
 * printf("%s\n", arg_dstr_cstr(ds)); // Output: Hello, Argtable!
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds        Pointer to the dynamic string object.
 * @param str       Pointer to the string to set as the new content.
 * @param free_proc Pointer to a function that will be used to free the string,
 *                  or one of the standard macros (`ARG_DSTR_STATIC`,
 *                  `ARG_DSTR_VOLATILE`, `ARG_DSTR_DYNAMIC`).
 *
 * @see arg_dstr_create, arg_dstr_cat, arg_dstr_cstr, arg_dstr_destroy
 */
ARG_EXTERN void arg_dstr_set(arg_dstr_t ds, char* str, arg_dstr_freefn* free_proc);

/**
 * Appends a string to the dynamic string object.
 *
 * The `arg_dstr_cat` function appends the specified null-terminated string to
 * the end of the dynamic string object (`arg_dstr_t`). This is useful for
 * building up complex strings incrementally, such as constructing help
 * messages, error messages, or logs.
 *
 * The dynamic string automatically grows as needed to accommodate the
 * additional content.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_cat(ds, "Hello, ");
 * arg_dstr_cat(ds, "world!");
 * printf("%s\n", arg_dstr_cstr(ds)); // Output: Hello, world!
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds  Pointer to the dynamic string object.
 * @param str Null-terminated string to append.
 *
 * @see arg_dstr_create, arg_dstr_catc, arg_dstr_catf, arg_dstr_cstr,
 *      arg_dstr_destroy
 */
ARG_EXTERN void arg_dstr_cat(arg_dstr_t ds, const char* str);

/**
 * Appends a single character to the dynamic string object.
 *
 * The `arg_dstr_catc` function appends the specified character to the end of
 * the dynamic string object (`arg_dstr_t`). This is useful for building up
 * strings character by character, such as when formatting output, constructing
 * error messages, or processing input streams.
 *
 * The dynamic string automatically grows as needed to accommodate additional
 * characters.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_catc(ds, 'A');
 * arg_dstr_catc(ds, 'B');
 * arg_dstr_catc(ds, 'C');
 * printf("%s\n", arg_dstr_cstr(ds)); // Output: ABC
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds Pointer to the dynamic string object.
 * @param c  The character to append.
 *
 * @see arg_dstr_create, arg_dstr_cat, arg_dstr_catf, arg_dstr_cstr,
 *      arg_dstr_destroy
 */
ARG_EXTERN void arg_dstr_catc(arg_dstr_t ds, char c);

/**
 * Appends a formatted string to the dynamic string object.
 *
 * The `arg_dstr_catf` function appends a formatted string, generated using
 * `printf`-style formatting, to the end of the specified dynamic string object
 * (`arg_dstr_t`). This allows you to efficiently build up complex strings with
 * variable content, such as error messages, help text, or logs.
 *
 * The function accepts a format string and a variable number of arguments,
 * similar to `printf`. The resulting formatted string is concatenated to the
 * current contents of the dynamic string.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_catf(ds, "Error: invalid value '%s' for option --%s\n", value, optname);
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds  Pointer to the dynamic string object.
 * @param fmt Format string (as in `printf`).
 * @param ... Additional arguments to format.
 *
 * @see arg_dstr_create, arg_dstr_cat, arg_dstr_cstr, arg_dstr_destroy
 */
ARG_EXTERN void arg_dstr_catf(arg_dstr_t ds, const char* fmt, ...);

/**
 * Retrieves the null-terminated string from a dynamic string object.
 *
 * The `arg_dstr_cstr` function returns a pointer to the internal
 * null-terminated character buffer managed by the dynamic string object
 * (`arg_dstr_t`). This allows you to access the current contents of the dynamic
 * string for printing, logging, or further processing.
 *
 * The returned pointer remains valid until the dynamic string is reset,
 * modified, or destroyed. Do not modify or free the returned string directly;
 * use the appropriate Argtable3 dynamic string functions for memory management.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_dstr_cat(ds, "Hello, ");
 * arg_dstr_cat(ds, "world!");
 * printf("%s\n", arg_dstr_cstr(ds)); // Output: Hello, world!
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds Dynamic string object.
 * @return   Pointer to the internal null-terminated string buffer.
 *
 * @see arg_dstr_create, arg_dstr_cat, arg_dstr_destroy
 */
ARG_EXTERN char* arg_dstr_cstr(arg_dstr_t ds);

/**
 * Initializes the sub-command mechanism.
 *
 * The `arg_cmd_init` function sets up the internal data structures required for
 * sub-command support in Argtable3. It must be called before registering,
 * dispatching, or iterating over sub-commands in your application.
 *
 * This function is typically called once at program startup, before any
 * sub-commands are registered. It ensures that the sub-command API is ready for
 * use. After initialization, you can use functions such as `arg_cmd_register`,
 * `arg_cmd_dispatch`, and `arg_cmd_itr_create`.
 *
 * When sub-command support is no longer needed (e.g., at program shutdown),
 * call `arg_cmd_uninit` to release resources and prevent memory leaks.
 *
 * Example usage:
 * ```
 * arg_cmd_init();
 * // Register and use sub-commands...
 * arg_cmd_uninit();
 * ```
 *
 * @see arg_cmd_uninit, arg_cmd_register, arg_cmd_unregister, arg_cmd_count
 */
ARG_EXTERN void arg_cmd_init(void);

/**
 * Releases resources and cleans up the sub-command mechanism.
 *
 * The `arg_cmd_uninit` function deinitializes the sub-command infrastructure,
 * freeing any memory or resources allocated for sub-command registration,
 * lookup, and iteration. After calling this function, all registered
 * sub-commands are removed, and the sub-command API can no longer be used until
 * `arg_cmd_init` is called again.
 *
 * This function should be called at program shutdown or when you no longer need
 * sub-command support, to prevent memory leaks and ensure proper cleanup.
 *
 * Example usage:
 * ```
 * arg_cmd_init();
 * // Register and use sub-commands...
 * arg_cmd_uninit();
 * ```
 *
 * @see arg_cmd_init, arg_cmd_register, arg_cmd_unregister, arg_cmd_count
 */
ARG_EXTERN void arg_cmd_uninit(void);

/**
 * Registers a new sub-command with the application.
 *
 * The `arg_cmd_register` function adds a sub-command to the application's
 * sub-command table, associating it with a handler function, a description, and
 * an optional context pointer. This enables applications to support multiple
 * commands (such as `git` or `docker`-style interfaces), where each sub-command
 * can have its own logic and help text.
 *
 * The handler function (`proc`) will be called when the sub-command is
 * dispatched via `arg_cmd_dispatch`. The description is used in help and usage
 * messages. The context pointer (`ctx`) can be used to pass user-defined data
 * to the handler.
 *
 * If a sub-command with the same name already exists, its registration will be
 * replaced with the new handler, description, and context.
 *
 * Example usage:
 * ```
 * // Register a "list" sub-command
 * arg_cmd_register("list", list_cmd, "List all items", NULL);
 *
 * // Register a "remove" sub-command with context
 * arg_cmd_register("remove", remove_cmd, "Remove an item", my_context_ptr);
 * ```
 *
 * @param name        The name of the sub-command (null-terminated string).
 * @param proc        Pointer to the handler function for the sub-command.
 * @param description A short description of the sub-command for help output.
 * @param ctx         Optional user-defined context pointer (may be NULL).
 *
 * @see arg_cmd_unregister, arg_cmd_dispatch, arg_cmd_info, arg_cmd_count
 */
ARG_EXTERN void arg_cmd_register(const char* name, arg_cmdfn proc, const char* description, void* ctx);

/**
 * Unregisters a sub-command by name.
 *
 * The `arg_cmd_unregister` function removes a previously registered sub-command
 * from the application's sub-command table. After calling this function, the
 * specified sub-command will no longer be available for dispatch, iteration, or
 * help output.
 *
 * This function is useful for applications that support dynamic registration
 * and removal of sub-commands at runtime, such as plugin-based tools or
 * interactive shells.
 *
 * If the sub-command with the given name does not exist, the function has no
 * effect.
 *
 * Example usage:
 * ```
 * arg_cmd_register("remove", remove_cmd, "Remove an item", NULL);
 * // ... later ...
 * arg_cmd_unregister("remove");
 * ```
 *
 * @param name The name of the sub-command to unregister (null-terminated
 * string).
 *
 * @see arg_cmd_register, arg_cmd_dispatch, arg_cmd_count, arg_cmd_info
 */
ARG_EXTERN void arg_cmd_unregister(const char* name);

/**
 * Dispatches a registered sub-command by name.
 *
 * The `arg_cmd_dispatch` function locates the sub-command with the specified
 * name and invokes its handler function, passing the provided command-line
 * arguments and a dynamic string buffer for output or error messages.
 *
 * This function is typically used in applications that support multiple
 * sub-commands (such as `git` or `docker`-style interfaces), allowing you to
 * delegate command-line processing to the appropriate handler based on the
 * user's input.
 *
 * If the sub-command is found, its handler function is called with the given
 * arguments and context. If the sub-command is not found, an error message may
 * be written to the result buffer, and a nonzero error code is returned.
 *
 * Example usage:
 * ```
 * // Register sub-commands
 * arg_cmd_register("list", list_cmd, "List items", NULL);
 * arg_cmd_register("add", add_cmd, "Add an item", NULL);
 *
 * // Dispatch based on argv[1]
 * arg_dstr_t res = arg_dstr_create();
 * int ret = arg_cmd_dispatch(argv[1], argc - 1, argv + 1, res);
 * if (ret != 0) {
 *     fprintf(stderr, "%s\n", arg_dstr_cstr(res));
 * }
 * arg_dstr_destroy(res);
 * ```
 *
 * @param name The name of the sub-command to dispatch (null-terminated string).
 * @param argc The number of command-line arguments for the sub-command.
 * @param argv The array of command-line arguments for the sub-command.
 * @param res  Pointer to a dynamic string buffer for output or error messages.
 *
 * @return 0 if the sub-command was found and executed successfully, nonzero if
 *         the sub-command was not found or an error occurred.
 *
 * @see arg_cmd_register, arg_cmd_info, arg_cmd_count, arg_dstr_create,
 *      arg_dstr_cstr
 */
ARG_EXTERN int arg_cmd_dispatch(const char* name, int argc, char* argv[], arg_dstr_t res);

/**
 * Returns the number of registered sub-commands.
 *
 * The `arg_cmd_count` function returns the total number of sub-commands
 * currently registered in the application. This is useful for applications that
 * support multiple commands (such as `git` or `docker`-style interfaces) and
 * need to enumerate, display, or manage the available sub-commands.
 *
 * You can use this function to determine the size of the sub-command table,
 * iterate over all registered sub-commands, or display a summary of available
 * commands to the user.
 *
 * Example usage:
 * ```
 * unsigned int count = arg_cmd_count();
 * printf("There are %u sub-commands registered.\n", count);
 * ```
 *
 * @return The number of sub-commands currently registered.
 *
 * @see arg_cmd_register, arg_cmd_info, arg_cmd_itr_create
 */
ARG_EXTERN unsigned int arg_cmd_count(void);

/**
 * Retrieves information about a registered sub-command by name.
 *
 * The `arg_cmd_info` function returns a pointer to the `arg_cmd_info_t`
 * structure for the sub-command with the specified name. This structure
 * contains metadata such as the sub-command's name, description, handler
 * function, and context pointer.
 *
 * This function is useful for querying details about a specific sub-command,
 * such as when generating help output, dispatching commands, or inspecting
 * available sub-commands in applications that support multiple commands (e.g.,
 * `git` or `docker`-style interfaces).
 *
 * If the sub-command is not found, the function returns `NULL`.
 *
 * Example usage:
 * ```
 * arg_cmd_info_t* info = arg_cmd_info("list");
 * if (info) {
 *     printf("Sub-command: %s - %s\n", info->name, info->description);
 * }
 * ```
 *
 * @param name The name of the sub-command to look up (null-terminated string).
 * @return     Pointer to the `arg_cmd_info_t` structure for the sub-command, or
 *             `NULL` if the sub-command is not found.
 *
 * @see arg_cmd_register, arg_cmd_count, arg_cmd_itr_create, arg_cmd_info_t
 */
ARG_EXTERN arg_cmd_info_t* arg_cmd_info(const char* name);

/**
 * Creates a new sub-command iterator for traversing registered sub-commands.
 *
 * The `arg_cmd_itr_create` function allocates and initializes a sub-command
 * iterator, which can be used to traverse all registered sub-commands in the
 * application. This iterator provides a convenient way to enumerate, search, or
 * process sub-commands, especially in applications that support multiple
 * commands (such as `git` or `docker`-style interfaces).
 *
 * After creating the iterator, you can use functions like `arg_cmd_itr_advance`
 * to move through the sub-command table, and `arg_cmd_itr_key` or
 * `arg_cmd_itr_value` to access the current sub-command's name or metadata.
 * When finished, call `arg_cmd_itr_destroy` to release resources.
 *
 * Example usage:
 * ```
 * arg_cmd_itr_t itr = arg_cmd_itr_create();
 * while (arg_cmd_itr_advance(itr)) {
 *     const char* key = arg_cmd_itr_key(itr);
 *     printf("Sub-command: %s\n", key);
 * }
 * arg_cmd_itr_destroy(itr);
 * ```
 *
 * @return A handle to the newly created sub-command iterator, or `NULL` if
 *         allocation fails.
 *
 * @see arg_cmd_itr_destroy, arg_cmd_itr_advance, arg_cmd_itr_key,
 *      arg_cmd_itr_value
 */
ARG_EXTERN arg_cmd_itr_t arg_cmd_itr_create(void);

/**
 * Destroys the sub-command iterator and releases associated resources.
 *
 * The `arg_cmd_itr_destroy` function deallocates any memory or resources
 * associated with a sub-command iterator created by `arg_cmd_itr_create`. After
 * calling this function, the iterator is no longer valid and must not be used.
 *
 * This function should be called when you are finished iterating over
 * sub-commands to prevent memory leaks and ensure proper cleanup.
 *
 * Example usage:
 * ```
 * arg_cmd_itr_t itr = arg_cmd_itr_create();
 * while (arg_cmd_itr_advance(itr)) {
 *     // process sub-commands
 * }
 * arg_cmd_itr_destroy(itr);
 * ```
 *
 * @param itr The sub-command iterator to destroy.
 *
 * @see arg_cmd_itr_create, arg_cmd_itr_advance, arg_cmd_itr_key,
 *      arg_cmd_itr_value
 */
ARG_EXTERN void arg_cmd_itr_destroy(arg_cmd_itr_t itr);

/**
 * Advances the sub-command iterator to the next entry.
 *
 * The `arg_cmd_itr_advance` function moves the sub-command iterator to the next
 * sub-command in the table. This allows you to iterate over all registered
 * sub-commands in your application, enabling tasks such as listing available
 * commands, generating help output, or performing batch operations.
 *
 * This function is typically used in a loop, where you repeatedly call
 * `arg_cmd_itr_advance` and retrieve the current sub-command's key or value
 * using `arg_cmd_itr_key` or `arg_cmd_itr_value`.
 *
 * Example usage:
 * ```
 * arg_cmd_itr_t itr = arg_cmd_itr_create();
 * while (arg_cmd_itr_advance(itr)) {
 *     const char* key = arg_cmd_itr_key(itr);
 *     printf("Sub-command: %s\n", key);
 * }
 * arg_cmd_itr_destroy(itr);
 * ```
 *
 * @param itr The sub-command iterator.
 * @return    1 if the iterator was advanced to a valid entry, 0 if there are no
 *            more entries (end of the sub-command table).
 *
 * @see arg_cmd_itr_create, arg_cmd_itr_key, arg_cmd_itr_value, arg_cmd_info_t
 */
ARG_EXTERN int arg_cmd_itr_advance(arg_cmd_itr_t itr);

/**
 * Retrieves the key (name) of the sub-command at the current iterator position.
 *
 * The `arg_cmd_itr_key` function returns a pointer to the key (typically the
 * sub-command name as a null-terminated string) at the current position of the
 * sub-command iterator. This allows you to access the identifier for the
 * sub-command currently referenced by the iterator, which is useful for
 * displaying, comparing, or processing sub-commands in applications that
 * support multiple commands.
 *
 * This function is typically used when iterating over all registered
 * sub-commands or after searching for a specific sub-command using the
 * iterator.
 *
 * Example usage:
 * ```
 * arg_cmd_itr_t itr = arg_cmd_itr_create();
 * while (arg_cmd_itr_advance(itr)) {
 *     const char* key = arg_cmd_itr_key(itr);
 *     printf("Sub-command: %s\n", key);
 * }
 * arg_cmd_itr_destroy(itr);
 * ```
 *
 * @param itr The sub-command iterator.
 * @return    Pointer to the key (sub-command name) at the current iterator
 *            position, or `NULL` if the iterator is not valid or at the end.
 *
 * @see arg_cmd_itr_create, arg_cmd_itr_advance, arg_cmd_itr_value,
 *      arg_cmd_info_t
 */
ARG_EXTERN char* arg_cmd_itr_key(arg_cmd_itr_t itr);

/**
 * Retrieves the value (sub-command information) at the current iterator
 * position.
 *
 * The `arg_cmd_itr_value` function returns a pointer to the `arg_cmd_info_t`
 * structure at the current position of the sub-command iterator. This allows
 * you to access metadata and handler information for the sub-command found or
 * iterated to by the iterator.
 *
 * This function is typically used after advancing the iterator or searching for
 * a specific sub-command, enabling you to inspect the sub-command's name,
 * description, handler function, and context pointer.
 *
 * Example usage:
 * ```
 * arg_cmd_itr_t itr = arg_cmd_itr_create();
 * if (arg_cmd_itr_search(itr, "list")) {
 *     arg_cmd_info_t* info = arg_cmd_itr_value(itr);
 *     printf("Found sub-command: %s - %s\n", info->name, info->description);
 * }
 * arg_cmd_itr_destroy(itr);
 * ```
 *
 * @param itr The sub-command iterator.
 * @return    Pointer to the `arg_cmd_info_t` structure at the current iterator
 *            position, or `NULL` if the iterator is not valid or at the end.
 *
 * @see arg_cmd_itr_create, arg_cmd_itr_advance, arg_cmd_itr_search,
 *      arg_cmd_info_t
 */
ARG_EXTERN arg_cmd_info_t* arg_cmd_itr_value(arg_cmd_itr_t itr);

/**
 * Searches for a sub-command by key using the sub-command iterator.
 *
 * The `arg_cmd_itr_search` function searches the sub-command table for a
 * sub-command whose key matches the specified value, starting from the current
 * position of the iterator. If a match is found, the iterator is positioned at
 * the found entry.
 *
 * This function is useful for efficiently locating a specific sub-command in
 * applications that support multiple commands (such as `git` or `docker`-style
 * interfaces), especially when iterating over or managing a dynamic set of
 * sub-commands.
 *
 * Example usage:
 * ```
 * arg_cmd_itr_t itr = arg_cmd_itr_create();
 * if (arg_cmd_itr_search(itr, "list")) {
 *     arg_cmd_info_t* info = arg_cmd_itr_value(itr);
 *     printf("Found sub-command: %s - %s\n", info->name, info->description);
 * }
 * arg_cmd_itr_destroy(itr);
 * ```
 *
 * @param itr The sub-command iterator.
 * @param k   The key to search for (typically a string with the sub-command
 * name).
 * @return    1 if the key is found and the iterator is positioned at the entry,
 *            0 otherwise.
 *
 * @see arg_cmd_itr_create, arg_cmd_itr_value, arg_cmd_info_t
 */
ARG_EXTERN int arg_cmd_itr_search(arg_cmd_itr_t itr, void* k);

/**
 * Sorts an array using a custom comparison function.
 *
 * The `arg_mgsort` function performs a merge sort on the specified array,
 * allowing you to sort elements of any type using a user-provided comparison
 * function. This is useful for sorting sub-command tables, argument arrays, or
 * any other data structures used within Argtable3 or your application.
 *
 * The function is flexible and can sort arrays of arbitrary element size. The
 * comparison function should return an integer less than, equal to, or greater
 * than zero if the first argument is considered to be respectively less than,
 * equal to, or greater than the second.
 *
 * Example usage:
 * ```
 * // Sort an array of sub-command info structures by name
 * arg_mgsort(cmd_array, cmd_count, sizeof(arg_cmd_info_t), 0, cmd_count - 1, compare_cmd_info);
 * ```
 *
 * @param data      Pointer to the array to be sorted.
 * @param size      Number of elements in the array.
 * @param esize     Size of each element in bytes.
 * @param i         Starting index of the range to sort (typically 0).
 * @param k         Ending index of the range to sort (typically size - 1).
 * @param comparefn Pointer to a comparison function that takes two `const
 *                  void*` arguments and returns an int (like `strcmp` or
 *                  `memcmp`).
 *
 * @see arg_cmd_info_t, arg_cmd_itr_t
 */
ARG_EXTERN void arg_mgsort(void* data, int size, int esize, int i, int k, arg_comparefn* comparefn);

/**
 * Generates and retrieves the default help message for the application.
 *
 * The `arg_make_get_help_msg` function constructs a standard help message
 * describing the usage, options, and arguments for the main application or
 * module. The generated help text is written to a dynamic string buffer, which
 * can be displayed to the user, printed to the console, or included in
 * documentation.
 *
 * This function is typically used to provide users with a quick reference to
 * the application's command-line interface, especially when no specific command
 * or argument table is provided. It is useful for displaying help output in
 * response to `--help` or similar flags.
 *
 * The help message typically includes the application name and its usage
 * syntax, a glossary of available options and arguments with descriptions, and
 * any additional remarks or formatting defined in the argument table.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_make_get_help_msg(ds);
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param res Dynamic string handle to store the generated help message.
 *
 * @see arg_make_help_msg, arg_dstr_create, arg_dstr_cstr
 */
ARG_EXTERN void arg_make_get_help_msg(arg_dstr_t res);

/**
 * Generates a formatted help message for the command-line interface.
 *
 * The `arg_make_help_msg` function constructs a comprehensive help message
 * describing the usage, options, and arguments for a command-line application
 * or sub-command. The generated help text is written to a dynamic string
 * buffer, which can be displayed to the user, printed to the console, or
 * included in documentation.
 *
 * The help message typically includes the command or sub-command name and its
 * usage syntax, a glossary of available options and arguments with descriptions,
 * and any additional formatting or custom remarks defined in the argument table.
 *
 * This function is useful for providing users with clear guidance on how to
 * invoke the application and what options are available. It is commonly called
 * when the user requests help (e.g., with `-h` or `--help`).
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * arg_make_help_msg(ds, "myapp", argtable);
 * printf("%s", arg_dstr_cstr(ds));
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds        Dynamic string to store the generated help message.
 * @param cmd_name  Name of the command or sub-command to display in the usage
 *                  line.
 * @param argtable  Array of argument table structs describing the available
 *                  options and arguments.
 *
 * @see arg_dstr_create, arg_dstr_cstr, arg_rem, arg_print_glossary
 */
ARG_EXTERN void arg_make_help_msg(arg_dstr_t ds, const char* cmd_name, void** argtable);

/**
 * Generates a concise syntax error message for command-line parsing errors.
 *
 * The `arg_make_syntax_err_msg` function constructs a brief error message
 * summarizing the syntax errors detected during argument parsing. The message
 * is written to a dynamic string buffer, which can then be displayed to the
 * user or logged for diagnostics. This function is useful for applications that
 * want to provide immediate feedback about what went wrong, without including
 * full usage or help information.
 *
 * The generated message typically includes a summary of the errors encountered
 * and the relevant arguments or options that caused the error.
 *
 * For a more comprehensive help message that includes usage and glossary
 * information, use `arg_make_syntax_err_help_msg`.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors > 0) {
 *     arg_make_syntax_err_msg(ds, argtable, end);
 *     fprintf(stderr, "%s", arg_dstr_cstr(ds));
 * }
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds       Pointer to a dynamic string object to store the generated
 *                 message.
 * @param argtable An array of argument table structs describing the expected
 *                 arguments.
 * @param end      Pointer to the `arg_end` structure containing error details.
 *
 * @see arg_make_syntax_err_help_msg, arg_parse, arg_print_errors,
 *      arg_dstr_create, arg_dstr_cstr
 */
ARG_EXTERN void arg_make_syntax_err_msg(arg_dstr_t ds, void** argtable, arg_end_t* end);

/**
 * Generates a detailed syntax error help message for command-line parsing errors.
 *
 * The `arg_make_syntax_err_help_msg` function constructs a comprehensive help
 * message when syntax errors are detected during argument parsing. It combines
 * error details, usage information, and optional help text into a dynamic string
 * buffer, which can then be displayed to the user or logged for diagnostics.
 *
 * This function is typically used in applications that want to provide users
 * with clear feedback about what went wrong, what the correct syntax is, and
 * how to get further help. It is especially useful for CLI tools that need to
 * guide users through complex argument requirements.
 *
 * The generated message may include the command name and a summary of the
 * errors, the correct usage syntax for the command, a glossary of available
 * options and arguments, and additional help text if the help flag is set.
 *
 * Example usage:
 * ```
 * arg_dstr_t ds = arg_dstr_create();
 * int exitcode = 0;
 * int nerrors = arg_parse(argc, argv, argtable);
 * if (nerrors > 0) {
 *     arg_make_syntax_err_help_msg(ds, argv[0], 0, nerrors, argtable, end,
 *                                  &exitcode);
 *     fprintf(stderr, "%s", arg_dstr_cstr(ds));
 * }
 * arg_dstr_destroy(ds);
 * ```
 *
 * @param ds       Pointer to a dynamic string object to store the generated
 *                 message.
 * @param name     The name of the command or application.
 * @param help     Nonzero if the help flag was specified; zero otherwise.
 * @param nerrors  The number of syntax errors detected during parsing.
 * @param argtable An array of argument table structs describing the expected
 *                 arguments.
 * @param end      Pointer to the `arg_end` structure containing error details.
 * @param exitcode Pointer to an integer where the recommended exit code will be
 *                 stored.
 *
 * @return Returns 0 on success, or a nonzero error code on failure.
 *
 * @see arg_parse, arg_print_errors, arg_dstr_create, arg_dstr_cstr
 */
ARG_EXTERN int arg_make_syntax_err_help_msg(arg_dstr_t ds, const char* name, int help, int nerrors, void** argtable, arg_end_t* end, int* exitcode);

/**
 * Sets the module (application) name.
 *
 * The `arg_set_module_name` function allows you to specify the name of your
 * application or module. This name can be displayed in help messages, version
 * output, and error messages generated by Argtable3. It is typically called at
 * program startup, before parsing command-line arguments, to ensure that the
 * module name is available for reporting to the user.
 *
 * Setting a meaningful module name helps users identify the application in
 * command-line output and documentation, especially when distributing binaries
 * or providing support.
 *
 * Example usage:
 * ```
 * arg_set_module_name("myapp");
 * ```
 *
 * @param name The module or application name as a null-terminated string. Pass
 *             a descriptive name that will be shown in help and version output.
 */
ARG_EXTERN void arg_set_module_name(const char* name);

/**
 * Sets the module (application) version information.
 *
 * The `arg_set_module_version` function allows you to specify the version
 * information for your application or module, which can be displayed in help or
 * version output. This function is typically called at program startup, before
 * parsing command-line arguments, to ensure that version information is
 * available for reporting to the user.
 *
 * The version is specified using three integer components (major, minor, patch)
 * and an optional string tag (such as a commit hash or build identifier). This
 * enables you to provide detailed versioning information, which is useful for
 * debugging, support, and user reference.
 *
 * Example usage:
 * ```
 * arg_set_module_version(3, 1, 5, "githash-abc123");
 * ```
 *
 * @param[in] major The major version number (e.g., 3 in version 3.1.5).
 * @param[in] minor The minor version number (e.g., 1 in version 3.1.5).
 * @param[in] patch The patch version number (e.g., 5 in version 3.1.5).
 * @param[in] tag   An optional string identifying a version control commit,
 *                  build tag, or other metadata. Pass `NULL` if not needed.
 */
ARG_EXTERN void arg_set_module_version(int major, int minor, int patch, const char* tag);

/**** deprecated functions, for back-compatibility only ********/

/**
 * Deallocates or frees non-null entries of the argument table.
 *
 * The `arg_free` function is deprecated in favour of the `arg_freetable`
 * function due to a flaw in its design. The flaw results in memory leak in the
 * (very rare) case that an intermediate entry in the argtable array failed its
 * memory allocation while others following that entry were still allocated ok.
 * Those subsequent allocations will not be deallocated by `arg_free`.
 *
 * Despite the unlikeliness of the problem occurring, and the even unlikelier
 * event that it has any deliterious effect, it is fixed regardless by replacing
 * `arg_free` with the newer `arg_freetable` function. We still keep `arg_free`
 * for backwards compatibility.
 *
 * @param argtable An array of argument table structs.
 * @deprecated Use `arg_freetable` instead.
 */
ARG_EXTERN void arg_free(void** argtable);

#ifdef __cplusplus
}
#endif
#endif
