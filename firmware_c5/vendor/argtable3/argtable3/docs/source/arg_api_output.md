# Output

The _Argtable3_ library provides a versatile API for formatting and printing
command-line argument information. These functions are designed to help
developers generate user-friendly output, such as usage instructions, error
messages, and detailed glossaries for command-line applications.

The output API simplifies the process of presenting argument details in a
structured and readable format. It supports various output styles, including
concise syntax descriptions, verbose usage instructions, and GNU-style
glossaries. Additionally, the API provides functions for printing errors and
formatting text dynamically.

## Key Features

- **Option Printing**: Use `arg_print_option` and `arg_print_option_ds` to print
  individual command-line options in a structured format.
- **Syntax Descriptions**: Functions like `arg_print_syntax`,
  `arg_print_syntax_ds`, `arg_print_syntaxv`, and `arg_print_syntaxv_ds`
  generate concise or verbose usage syntax for commands.
- **Glossary Generation**: Create detailed glossaries of command-line arguments
  using `arg_print_glossary`, `arg_print_glossary_ds`, `arg_print_glossary_gnu`,
  and `arg_print_glossary_gnu_ds`.
- **Error Reporting**: Print error messages with `arg_print_errors` and
  `arg_print_errors_ds` to help users identify and resolve issues with their
  input.
- **Custom Formatting**: Use `arg_print_formatted` to dynamically format and
  print text with custom margins and alignment.

This API is designed to enhance the user experience by providing clear and
informative output for command-line applications. Below is a detailed reference
for each function in the output API.

## API

:::{doxygenfunction} arg_print_option
:::

:::{doxygenfunction} arg_print_option_ds
:::

:::{doxygenfunction} arg_print_syntax
:::

:::{doxygenfunction} arg_print_syntax_ds
:::

:::{doxygenfunction} arg_print_syntaxv
:::

:::{doxygenfunction} arg_print_syntaxv_ds
:::

:::{doxygenfunction} arg_print_glossary
:::

:::{doxygenfunction} arg_print_glossary_ds
:::

:::{doxygenfunction} arg_print_glossary_gnu
:::

:::{doxygenfunction} arg_print_glossary_gnu_ds
:::

:::{doxygenfunction} arg_print_errors
:::

:::{doxygenfunction} arg_print_errors_ds
:::

:::{doxygenfunction} arg_print_formatted
:::

:::{doxygenfunction} arg_make_get_help_msg
:::

:::{doxygenfunction} arg_make_help_msg
:::

:::{doxygenfunction} arg_make_syntax_err_msg
:::

:::{doxygenfunction} arg_make_syntax_err_help_msg
:::
