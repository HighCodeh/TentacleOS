# Installation

You can embed the amalgamation source files in your projects, add Argtable3 as a
dependency in the vcpkg manifest, install Argtable3 as a system-wide CMake
package, or build the library from release archives.

## Embed Amalgamation Source Files

:::{note}
We no longer provide the amalgamation source files (`argtable3.c` and
`argtable3.h`) in the repository. You can get the amalgamation distribution
either from the release page (`argtable-<version>-amalgamation.(zip|tar.gz)`),
or generate the distribution yourself by using the generator under the `tools`
directory:

1. Navigate to the `tools` directory.
2. Run `./build dist`, which will generate the distribution under the `<ROOT>/dist`
   directory.
:::

Add `argtable3.c` and `argtable3.h` from the amalgamation distribution to your
projects. This is the simplest and recommended way to use Argtable3: it not only
removes the hassle of building the library, but also allows compilers to do
better inter-procedure optimization.


## Install for a Single Project with vcpkg Manifest

[vcpkg](https://vcpkg.io) is an open source C/C++ package manager based on
CMake, and it supports certain stable releases of Argtable3. To add the library
to your CMake project, it's recommended to add vcpkg as a submodule to your
project repo and use it to manage project dependencies. All libraries installed
in this way can only be consumed by the project and won't impact other projects
in the system.

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

## Install for All Projects with vcpkg

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


## Build from Release Archives or Source

If none of the methods above suits your needs, or if you want to help developing
Argtable3, you can always build from archives on the release page or from the
repository.

* If you use GCC (Linux, MacOSX, MinGW, Cygwin), run:

  ```
  $ mkdir build
  $ cd build
  $ cmake -DCMAKE_BUILD_TYPE=Debug ..
  $ make
  $ make test
  ```

  Makefile-based generators in CMake only support one configuration at a time,
  so you need to specify `CMAKE_BUILD_TYPE` to `Debug`, `Release`, `MinSizeRel`,
  or `RelWithDebInfo`. To build multiple configurations, you need to create a
  build directory for each configuraiton.

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

* If you use Microsoft Visual C++ compiler, run:

  ```
  $ mkdir build
  $ cd build
  $ cmake -G "Visual Studio 15 2017 Win64" ..
  $ cmake --build . --config Debug
  $ ctest -C Debug
  ```

  You can also use Visual Studio 2017 IDE to open the generated solution. To
  cleanup, just remove the `build` directory.


To build a tagged version, go to the project root directory, and use the
`Makefile` in the project root folder to check out the specified version:

  ```
  $ make taglist
  Available TAGs:
  v3.1.1.432a160
  $ make co TAG=v3.1.1.432a160
  $ cd .tags/v3.1.1.432a160
  $ mkdir build
  $ cd build
  $ cmake ..
  $ make
  $ make test
  ```

You will find the shared library (or Windows DLL), static library, and the
amalgamation distribution under the build directory.
