# Parsing

The _Argtable3_ library provides a comprehensive API for parsing command-line
options in a structured and efficient manner. This API simplifies the process of
defining, validating, and processing command-line arguments, making it easier to
build robust and user-friendly command-line applications.

The parsing API supports a wide range of argument types, including boolean
flags, integers, doubles, strings, regular expressions, file paths, and dates.
It also provides utility functions for managing argument tables, checking for
null arguments, and freeing allocated resources.

## Key Features

- **Core Parsing Functions**: Functions like `arg_parse` handle the parsing of
  command-line arguments, while `arg_nullcheck` ensures the validity of argument
  tables.
- **Argument Table Management**: Use `arg_end` to define the end of an argument
  table and `arg_freetable` to release resources associated with it.
- **Support for Multiple Argument Types**:
  - **Boolean Options**: Functions such as `arg_lit0`, `arg_lit1`, and
    `arg_litn` handle boolean flags.
  - **Integer Options**: Functions like `arg_int0`, `arg_int1`, and `arg_intn`
    parse integer arguments.
  - **Double Options**: Use `arg_dbl0`, `arg_dbl1`, and `arg_dbln` for
    floating-point arguments.
  - **String Options**: Functions such as `arg_str0`, `arg_str1`, and `arg_strn`
    manage string arguments.
  - **Regex Options**: Use `arg_rex0`, `arg_rex1`, and `arg_rexn` to parse
    arguments matching regular expressions.
  - **File Options**: Functions like `arg_file0`, `arg_file1`, and `arg_filen`
    handle file paths.
  - **Date Options**: Use `arg_date0`, `arg_date1`, and `arg_daten` to parse
    date arguments.
- **Miscellaneous**: The `arg_rem` function allows for defining custom argument
  types.

This API is designed to provide flexibility and ease of use, enabling developers
to focus on the functionality of their applications rather than the intricacies
of argument parsing. Below is a detailed reference for each function in the
command-line parsing API.

## Data Types

:::{doxygentypedef} arg_hdr_t
:::

:::{doxygentypedef} arg_rem_t
:::

:::{doxygentypedef} arg_lit_t
:::

:::{doxygentypedef} arg_int_t
:::

:::{doxygentypedef} arg_dbl_t
:::

:::{doxygentypedef} arg_str_t
:::

:::{doxygentypedef} arg_rex_t
:::

:::{doxygentypedef} arg_file_t
:::

:::{doxygentypedef} arg_date_t
:::

:::{doxygentypedef} arg_end_t
:::


## API

:::{doxygenfunction} arg_parse
:::

:::{doxygenfunction} arg_nullcheck
:::

:::{doxygenfunction} arg_end
:::

:::{doxygenfunction} arg_freetable
:::

### Flag Option API

:::{doxygenfunction} arg_lit0
:outline:
:::

:::{doxygenfunction} arg_lit1
:outline:
:::

:::{doxygenfunction} arg_litn
:::

### Integer Option API

:::{doxygenfunction} arg_int0
:outline:
:::

:::{doxygenfunction} arg_int1
:outline:
:::

:::{doxygenfunction} arg_intn
:::

### Double Option API

:::{doxygenfunction} arg_dbl0
:outline:
:::

:::{doxygenfunction} arg_dbl1
:outline:
:::

:::{doxygenfunction} arg_dbln
:::

### String Option API

:::{doxygenfunction} arg_str0
:outline:
:::

:::{doxygenfunction} arg_str1
:outline:
:::

:::{doxygenfunction} arg_strn
:::

### Regular Expression Option API

:::{doxygenfunction} arg_rex0
:outline:
:::

:::{doxygenfunction} arg_rex1
:outline:
:::

:::{doxygenfunction} arg_rexn
:::

### File Option API

:::{doxygenfunction} arg_file0
:outline:
:::

:::{doxygenfunction} arg_file1
:outline:
:::

:::{doxygenfunction} arg_filen
:::

### Date Option API

:::{doxygenfunction} arg_date0
:outline:
:::

:::{doxygenfunction} arg_date1
:outline:
:::

:::{doxygenfunction} arg_daten
:::

### Miscellaneous API

:::{doxygenfunction} arg_rem
:::
