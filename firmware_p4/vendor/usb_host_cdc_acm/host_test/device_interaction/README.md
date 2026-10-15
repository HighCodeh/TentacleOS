| Supported Targets | Linux |
| ----------------- | ----- |

# Description

This directory contains test code for `USB Host CDC-ACM` driver. Namely:

- Interactions with Mocked device added to the CDC-ACM driver (Device open, send mocked transfers, device close)

Tests are written using [Catch2](https://github.com/catchorg/Catch2) test framework, use CMock, so you must install Ruby on your machine to run them.

This test directory uses freertos as real component

# Build

Tests build regularly like an idf project. Currently only working on Linux machines.

```
idf.py --preview set-target linux
idf.py build
```

# Run

The build produces an executable in the build folder.

Just run:

```
idf.py monitor
```

or run the executable directly:

```
./build/host_test_usb_cdc.elf
```
