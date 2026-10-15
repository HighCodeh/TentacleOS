# Dynamic String

The _Argtable3_ library provides a set of utility functions for managing dynamic
strings. These functions are designed to simplify the process of building and
manipulating strings, particularly for constructing the final output (e.g.,
`stdout` results) in command handler functions.

Dynamic strings are flexible, resizable strings that allow developers to
efficiently concatenate, format, and manage text without worrying about manual
memory allocation or buffer sizes. The dynamic string API in Argtable3 ensures
that string operations are safe, efficient, and easy to use.

## Overview of Functions

- **Creation and Destruction**: Use `arg_dstr_create` to initialize a dynamic
  string and `arg_dstr_destroy` or `arg_dstr_free` to release its resources.
- **Resetting and Setting**: Reset the contents of a dynamic string with
  `arg_dstr_reset` or set its value with `arg_dstr_set`.
- **Concatenation**: Append strings, characters, or formatted text using
  `arg_dstr_cat`, `arg_dstr_catc`, and `arg_dstr_catf`.
- **Accessing the String**: Retrieve the underlying C-style string with
  `arg_dstr_cstr`.

This API is particularly useful for building complex command-line interfaces,
where dynamic and flexible string manipulation is required. Below is a detailed
reference for each function in the dynamic string API.

## API

:::{doxygenfunction} arg_dstr_create
:::

:::{doxygenfunction} arg_dstr_destroy
:::

:::{doxygenfunction} arg_dstr_reset
:::

:::{doxygenfunction} arg_dstr_free
:::

:::{doxygenfunction} arg_dstr_set
:::

:::{doxygenfunction} arg_dstr_cat
:::

:::{doxygenfunction} arg_dstr_catc
:::

:::{doxygenfunction} arg_dstr_catf
:::

:::{doxygenfunction} arg_dstr_cstr
:::
