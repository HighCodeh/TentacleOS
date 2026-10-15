# Sub-Command Management

The _Argtable3_ library provides a robust API for managing sub-commands in
command-line applications. This API allows developers to define, register, and
dispatch sub-commands, making it easier to implement multi-command interfaces
similar to tools like `git` or `docker`.

Sub-commands are individual commands within a larger command-line application,
each with its own arguments and behavior. The functions in this API enable the
creation, iteration, and management of these sub-commands, offering flexibility
and control over their lifecycle.

## Overview of Functions

- **Initialization and Cleanup**: Functions like `arg_cmd_init` and
  `arg_cmd_uninit` handle the setup and teardown of the sub-command system.
- **Registration and Unregistration**: Use `arg_cmd_register` and
  `arg_cmd_unregister` to add or remove sub-commands dynamically.
- **Dispatching Commands**: The `arg_cmd_dispatch` function executes the
  appropriate sub-command based on user input.
- **Command Information**: Retrieve details about registered sub-commands using
  `arg_cmd_info` or count them with `arg_cmd_count`.
- **Iterators**: Functions such as `arg_cmd_itr_create`, `arg_cmd_itr_destroy`,
  `arg_cmd_itr_advance`, `arg_cmd_itr_key`, `arg_cmd_itr_value`, and
  `arg_cmd_itr_search` provide tools for iterating over and searching through
  sub-commands.

This API is designed to simplify the implementation of complex command-line
interfaces, enabling developers to focus on the functionality of their commands
rather than the underlying infrastructure. Below is a detailed reference for
each function in the sub-command management API.

## Data Types

:::{doxygentypedef} arg_cmdfn
:::

:::{doxygentypedef} arg_cmd_info_t
:::

## API

:::{doxygenfunction} arg_cmd_init
:::

:::{doxygenfunction} arg_cmd_uninit
:::

:::{doxygenfunction} arg_cmd_register
:::

:::{doxygenfunction} arg_cmd_unregister
:::

:::{doxygenfunction} arg_cmd_dispatch
:::

:::{doxygenfunction} arg_cmd_count
:::

:::{doxygenfunction} arg_cmd_info
:::

:::{doxygenfunction} arg_cmd_itr_create
:::

:::{doxygenfunction} arg_cmd_itr_destroy
:::

:::{doxygenfunction} arg_cmd_itr_advance
:::

:::{doxygenfunction} arg_cmd_itr_key
:::

:::{doxygenfunction} arg_cmd_itr_value
:::

:::{doxygenfunction} arg_cmd_itr_search
:::
