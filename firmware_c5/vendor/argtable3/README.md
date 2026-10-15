[![clang-tidy](https://github.com/argtable/argtable3/actions/workflows/clang-tidy.yml/badge.svg)](https://github.com/argtable/argtable3/actions/workflows/clang-tidy.yml)
[![coverity](https://github.com/argtable/argtable3/actions/workflows/coverity.yml/badge.svg)](https://github.com/argtable/argtable3/actions/workflows/coverity.yml)
[![asan](https://github.com/argtable/argtable3/actions/workflows/asan.yml/badge.svg)](https://github.com/argtable/argtable3/actions/workflows/asan.yml)
[![valgrind](https://github.com/argtable/argtable3/actions/workflows/valgrind.yml/badge.svg)](https://github.com/argtable/argtable3/actions/workflows/valgrind.yml)
[![license](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)


Introduction of Argtable3
=========================

**Argtable3** is an open-source ANSI C library that simplifies parsing GNU-style
command-line options. It provides a declarative API to define your command-line
syntax, and because it's built on the standard `getopt` library, it ensures 100%
GNU-compliant behavior. Argtable3 automatically generates the error-handling
logic and usage descriptions that are essential for any robust command-line
program, saving you from tedious boilerplate code.


Quick Start
-----------

There are several ways to use Argtable3 in your C/C++ projects:

- **Embed Amalgamation Source Files**: Download the amalgamation package and
  include `argtable3.c` and `argtable3.h` in your project. This is the simplest
  and recommended approach.

- **Use vcpkg**: Add Argtable3 as a dependency in your project's vcpkg manifest
  for single-project use, or install it globally with vcpkg for system-wide
  access.

- **Use Meson WrapDB**: Integrate Argtable3 into Meson-based projects using the
  Meson WrapDB.

- **Use Conan**: Add Argtable3 as a dependency in your Conanfile for projects
  using Conan package management.

- **Build from Source**: Download release archives or clone the repository to
  build Argtable3 manually. This is ideal for contributors or those needing
  custom features.


### Embed Amalgamation Source Files

Download the amalgamation package from the release page and add `argtable3.c`
and `argtable3.h` to your project. This approach is the simplest and recommended
way to use Argtable3, as it eliminates the need to build the library and enables
better inter-procedural optimization by the compiler.


### Use vcpkg for a Single Project

[vcpkg](https://vcpkg.io) is an open source C/C++ package manager based on
CMake, and it supports the latest releases of Argtable3. To add the library to
your CMake project, it's recommended to add vcpkg as a submodule to your project
repo and use it to manage project dependencies. All libraries installed in this
way can only be consumed by the project and won't impact other projects in the
system.

If your project is under `D:/projects/demo` and the vcpkg submodule is under
`D:/projects/demo/deps/vcpkg`, first you need to add Argtable3 to the manifest,
`D:/projects/demo/vcpkg.json`:

```
{
    "name": "demo",
    "version": "0.0.1",
    "dependencies": [
        {
            "name": "argtable3",
            "version>=": "3.2.1"
        }
    ],
    "builtin-baseline": "92b42c4c680defe94f1665a847d04ded890f372e"
}
```

To add Argtable3 to your CMake scripts, you need to integrate the local vcpkg to
CMake by setting the `CMAKE_TOOLCHAIN_FILE` variable. You also need to link to
the static VC runtime (`/MT` or `/MTd`) if you want to use the static library
version of Argtable3:

```
cmake_minimum_required(VERSION 3.18)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/deps/vcpkg/scripts/buildsystems/vcpkg.cmake
  CACHE STRING "Vcpkg toolchain file")

project(versionstest)

add_executable(main main.cpp)

find_package(Argtable3 CONFIG REQUIRED)
target_link_libraries(main PRIVATE argtable3::argtable3)

if(VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")
  set_property(TARGET main PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
```

Now you can run `cmake` to install Argtable3, configure and generate build
scripts, and build the project:

```
$ mkdir build
$ cd build
$ cmake .. -DVCPKG_TARGET_TRIPLET=x64-windows-static
$ cmake --build .
```

### Use vcpkg for All Projects

If you want to make Argtable3 available for all projects in the system, you can
clone vcpkg to any directory and install packages there. Assuming vcpkg has been
cloned in `D:/dev/vcpkg` and the directory has been added to `PATH`, you can
install the static library version of Argtable3 in `D:/dev/vcpkg/installed`:

```
$ vcpkg install argtable3:x64-windows-static
```

Since each developer may clone vcpkg in a different place, it may not be
appropriate to specify the `CMAKE_TOOLCHAIN_FILE` variable in `CMakeLists.txt`.
Therefore, you should remove setting the `CMAKE_TOOLCHAIN_FILE` variable in the
`CMakeLists.txt` example above, and set the variable in the command line:

```
$ mkdir build
$ cd build
$ cmake .. -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_TOOLCHAIN_FILE=D:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
$ cmake --build .
```


### Use Meson WrapDB

If you are using [Meson](https://mesonbuild.com) as your build system, you can
use the [Meson WrapDB](https://mesonbuild.com/Wrap-dependency-system-manual.html)
to add Argtable3 as a dependency. This allows Meson to manage downloading and
building Argtable3 automatically.

First, add the Argtable3 wrap file to your project's `subprojects` directory by
running the following command in your project root:

```bash
$ meson wrap install argtable3
```

Next, declare the dependency in your `meson.build` file. Meson will automatically find the subproject.

```meson
# meson.build
project('demo', 'c', version: '0.0.1')

# Find the argtable3 dependency. Meson will find it in the subprojects
# directory via the .wrap file.
argtable3_dep = dependency('argtable3', version: '>=3.3.1')

executable(
  'main',
  'main.c',
  dependencies: [argtable3_dep]
)
```

Now you can configure and build your project, and Meson will handle the Argtable3 dependency:

```bash
meson setup build
meson compile -C build
```


### Use Conan

If you are using [Conan](https://conan.io) as your package manager, you can add
Argtable3 as a dependency. Argtable3 is available on the [Conan Center
Index](https://conan.io/center/recipes/argtable3).

First, create a `conanfile.txt` in your project root with the following content.
This will use the modern `CMakeDeps` and `CMakeToolchain` generators.

```
[requires]
argtable3/3.3.1

[generators]
CMakeDeps
CMakeToolchain
```

Next, install the dependencies from your project root. This command will
download the package and generate the necessary CMake integration files in the
`build` directory.

```bash
conan install . --output-folder=build --build=missing -s build_type=Release
```

Now, you can use `find_package` in your `CMakeLists.txt` to find and link
against Argtable3.

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(demo C)

# Find the package configuration files generated by Conan
find_package(Argtable3 REQUIRED)

add_executable(main main.c)

# Link the imported target provided by Conan
target_link_libraries(main PRIVATE argtable3::argtable3)
```

Finally, configure and build your project, making sure to point CMake to the
toolchain file generated by Conan.

```bash
# From your project root
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```


### Build from Release Archives or Source

If none of the methods above suits your needs, or if you want to help developing
Argtable3, you can always build from archives on the release page or from the
repository.

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build --config Debug
$ ctest --test-dir build -C Debug
```

Makefile-based generators in CMake only support one configuration at a time,
so you need to specify `CMAKE_BUILD_TYPE` to `Debug`, `Release`, `MinSizeRel`,
or `RelWithDebInfo`. To build multiple configurations, you need to create a
build directory for each configuration.

Since v3.2.1, CMake scripts will check `BUILD_SHARED_LIBS` and build either
the static library or the dynamic library at a time. `BUILD_SHARED_LIBS` is
`OFF` by default, so if you want to build the dynamic library, you have to set
`BUILD_SHARED_LIBS` to `ON` explicitly:

```
$ cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=ON ..
```

To cleanup, run `make clean` or remove the build directory:

```
$ rm -rf build
```

To build a tagged version, go to the project root directory, and use the
`Makefile` in the project root to check out the specified tag:

```
$ make taglist
Available TAGs:
...
v3.3.1
...
$ make co TAG=v3.3.1
$ cd .archive/argtable-v3.3.1
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build --config Debug
$ ctest --test-dir build -C Debug
```


Documentation
-------------

To learn how to use the Argtable3 API:
- Visit the [official documentation](https://www.argtable.org/docs/).
- Explore examples in the `examples` directory.
- Review unit tests in the `tests` directory.

To build a local copy of the documentation, make sure you have installed Docker
and run the following command in the repository root. The generated HTML files
will be located in the `docs/build/html` directory.

```
$ make docs
```


Authors
-------

Argtable is Copyright (C) 1998-2001,2003-2011 Stewart Heitmann.
Parts are Copyright (C) 1989-1994, 1996-1999, 2001, 2003
  Free Software Foundation, Inc.

Argtable was written by Stewart Heitmann <sheitmann@users.sourceforge.net>

Argtable is now maintained by Tom G. Huang <tomghuang@gmail.com>
The project homepage of Argtable3 is https://www.argtable.org
The project homepage of argtable2 is https://argtable.sourceforge.io

Here is a list of contributors who have helped to improve argtable:

- **Nina Clemson**: Editing the original argtable-1.0 documentation.
- **Livio Bertacco**: For bug fixes and the argtable-2.x Visual C++ Makefiles.
- **Justin Dearing**: For bug fixes and Windows DLL support, plus code support for the Open Watcom compiler and help with the Mac OS X configuration.
- **Asa Packer**: Contributing bug fixes and upgrades to the Visual C++ Makefiles.
- **Danilo Cicerone**: For the Italian translation of "Introduction to Argtable-2x" on http://www.digitazero.org.
- **Uli Fouquet**: For configuration patches and documentation related to cross-compiling argtable from Unix to Windows, as well as providing the arg_print_glossary_gnu function.
- **Shachar Shemesh**: For Debian package integration and kick-starting the migration to automake/autoconf.
- **Jasper Lievisse Adriaanse**:  Maintaining the argtable package in OpenBSD ports.
- **Ulrich Mohr**: For bug fixes relating to Texas Instrument DSP platforms.
- **John Vickers**: For bug fixes relating to Solaris/Motorola platforms.
- **Steve O'Neil**: For bug fixes relating to Solaris/Motorola platforms.
- **Lori A. Pritchett-Sheats**: Fixing a makefile bug relating to "make dist".
- **Paolo Bormida**: For instructions on building argtable with date and regex support on Windows.
- **Michel Valin**: For bug fixes relating to the configure scripts on IBM AIX platforms and instructions on compiling the example code under AIX.
- **Steve Christensen**: Providing prebuilt packages for SPARC/Solaris and x86/Solaris platforms on www.sunfreeware.com.
- **Jess Portnoy**: Reworking the rpm package and integrating argtable into Fedora Linux.
- **Michael Brown**: Incorporating support for pkg-config into the autoconf scripts.
- **Alexander Lindert**: For extensions to the parser to support hex, octal and binary integer formats as well as KB/MB/GB suffixes.
- **Rob Zaborowski**: Providing build configuration files for CMake.
- **Moczik Gabor**: For bug fixes relating to the parsing of filepaths and filename extensions.
