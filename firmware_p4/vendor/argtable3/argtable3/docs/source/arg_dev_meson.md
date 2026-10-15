# Maintain Meson WrapDB

**Meson** is an open-source, high-performance build system designed to be fast,
user-friendly, and support multiple platforms and compilers. It aims to simplify
the build process for software projects. The **Meson WrapDB** is its integrated
dependency management system, providing __wrap__ files that define how to obtain
and build external libraries as subprojects. This allows Meson-based projects to
easily incorporate and manage dependencies like Argtable3.

This document guides maintainers on updating the Argtable3 subproject in the
Meson WrapDB when a new library version is released. This ensures Meson users
can easily access the latest Argtable3 updates.

## Phase 1: Preparation and Local Changes

First, you need to install prerequisites and clone a WrapDB repository where you
can make your changes to the `argtable3` port.

1. **Prerequisites:**
    * **Git:** You must have Git installed.
	* **Python:** Meson is written in Python and requires Python 3.7 or newer.
    * **Meson:** You need a working Python installation and the Meson build
        system. If you don't have Meson, install it with `pip` in the virtual
        environment:
        ```bash
        python -m venv .venv
        .venv\Scripts\activate     # On Windows
        # or
        source .venv/bin/activate  # On Linux/macOS
        pip install meson
        ```
    * **Ninja:** Meson uses Ninja as its primary backend for actually executing
        the build steps due to its speed and efficiency. You can install Ninja
        in the virtual environment:
        ```bash
        pip install ninja
        ```

2. **Fork and Clone WrapDB:**
    * Fork the `mesonbuild/wrapdb` repository on GitHub to your own account.
    * Clone your fork locally:
        ```bash
        git clone https://github.com/YOUR_USERNAME/wrapdb.git
        cd wrapdb
        ```

3. **Locate and Update the `argtable3` Port:**
    * **`subprojects/argtable3.wrap`:** This is the main file to update. Open it
      in a text editor.
        * `directory`: This should point to the directory where the source code
		  is located. Update it to match the new `source_filename` without the
		  file extension. (If `source_filename` is `argtable3-3.2.1.tar.gz`, set
		  `directory = argtable3-3.2.1`.)
		* `source_url`: Change this to the URL of the new release tarball or zip file.
        * `source_hash`: Update this with the SHA256 hash of the new release
          archive. You can calculate it with `sha256sum <downloaded_archive_file>`.
        * `source_filename`: Update this to the new filename.
    * **`subprojects/packagefiles/argtable3/meson.build`:** Review the build
      definition to see if it needs changes for the new version.

	**Example `argtable3.wrap` modification:**
	```ini
	# In subprojects/argtable3.wrap
	[wrap-file]
	directory = argtable-v3.3.0.116da6c
	source_url = https://github.com/argtable/argtable3/releases/download/v3.3.0.116da6c/argtable-v3.3.0.116da6c.tar.gz
	source_filename = argtable-v3.3.0.116da6c.tar.gz
	source_hash = 21d653eaab4b9dede76ceb0cb4e8878cf5cadb48ef9180c6a4d1a5cef1549e65
	patch_directory = argtable3

	[provide]
	argtable3 = argtable3_dep
	```

    ```{note}
    For small changes, you can
    directly modify the `argtable3.wrap` file to point to the latest version of
    the argtable3 library. For larger changes, you may need to modify
    `packagefiles/argtable3/meson.build` to ensure it builds correctly with the
    latest version of the library. You may also need to update the
    `packagefiles/argtable3/argtable3.def` file to reflect any changes in the
    library's public API.
    ```

## Phase 2: Create a Local Test Project

Now, create a separate "consumer" project that will use your updated wrap.

4. **Create a Project Directory (outside of your `wrapdb` clone):**
    ```bash
    mkdir ~/meson-test-app
    cd ~/meson-test-app
    ```

5. **Create a Simple C Source File (`sample.c`):**
    This program will link against `argtable3` to prove the subproject was built and integrated correctly.

    ```c
    // sample.c
    #include <stdio.h>
    #include "argtable3.h"

    int main(int argc, char *argv[]) {
        struct arg_lit *help = arg_lit0("h", "help", "display this help and exit");
        struct arg_end *end = arg_end(20);
        void *argtable[] = {help, end};

        int nerrors = arg_parse(argc, argv, argtable);

        if (help->count > 0) {
            printf("Usage: %s", argv[0]);
            arg_print_syntax(stdout, argtable, "\n");
            printf("\nSuccessfully tested local argtable3 wrap!\n");
            arg_free(argtable);
            return 0;
        }

        if (nerrors > 0) {
            arg_print_errors(stdout, end, argv[0]);
            return 1;
        }

        printf("Test successful. Try running with --help.\n");
        arg_free(argtable);
        return 0;
    }
    ```

6. **Create a `meson.build` File:** This file defines your test application and
    declares the dependency on `argtable3`, specifying it as a subproject
    fallback.

    ```meson
    # meson.build
    project('wrap test', 'c')

    # This is the key part: declare a dependency on 'argtable3'.
    # Meson will first look for it on the system (e.g., via pkg-config).
    # If not found, it will 'fallback' to finding a subproject named 'argtable3'.
    argtable3_dep = dependency('argtable3',
      version: '>=3.2.1', # Use the new version you are testing
      fallback: ['argtable3', 'argtable3_dep']
    )

    executable('sample', 'sample.c',
      dependencies: argtable3_dep
    )
    ```

## Phase 3: Perform the Local Test

This is where you connect your updated wrap from your local `wrapdb` clone to
your test project.

7. **Copy Your Updated Wrap into the Test Project:**
    * Create a `subprojects` directory inside your test project.
    * Copy your entire modified `argtable3` directory from your `wrapdb` clone into
        the new `subprojects` directory.
        ```bash
        # Inside ~/meson-test-app
        mkdir subprojects

        # Copy your updated wrap files from your local wrapdb clone
        # Replace /path/to/your/wrapdb/ with the actual path
        cp -r /path/to/your/wrapdb/subprojects/argtable3 ./subprojects/
        ```

        In the end, you should have a file structure like this:
        ```
        .
        ├── Makefile
        ├── meson.build
        ├── sample.c
        └── subprojects
            ├── argtable3.wrap
            └── packagefiles
                └── argtable3
                    ├── argtable3.def
                    └── meson.build
        ```

        When you run Meson, it will find the wrap file at
        `subprojects/argtable3.wrap` and use it *before* it ever tries to
        connect to the online WrapDB. This forces it to use your local, modified
        version.

8. **Run Meson and Build:**
    * Now, configure your test project with Meson.
        ```bash
        # Inside ~/meson-test-app
        meson setup build-static-debug -Ddefault_library=static -Db_vscrt=mtd --buildtype=debug
        ```
    * You should see output indicating that Meson found the subproject and is
      using it. It will download the new tarball using the URL from your
      modified wrap file, verify the new hash, and configure the project.
        ```
        ...
        Subproject argtable3: Pkg-config dependency 'argtable3' not found, falling back to subproject.
        Downloading subproject argtable3 from https://github.com/argtable/argtable3/releases/download/v3.2.1/argtable3-3.2.1.tar.gz
        ...
        ```
    * Compile the project:
        ```bash
        meson compile -C build-static-debug
        ```

    ```{note}
    You can find a complete example demonstrating this local testing setup,
    including a `Makefile` to verify building various configurations (debug,
    release, static) using Meson, in the following archives:

    *   For Linux and macOS: [meson-test.tar.gz](/_static/meson-test.tar.gz)
    *   For Windows: [meson-test.zip](/_static/meson-test.zip)
    ```

9. **Run the Test Application:**
    * If the build succeeds, run your test app to confirm it works.
        ```bash
        ./build/mytestapp --help
        ```
    * If it prints the help message from `argtable3`, your local test was
      successful! You have confirmed that the new version downloads, builds, and
      links correctly.

10. **Add the New Version to`releases.json`:**
    After making sure that the wrap works correctly, you need to update the
    `releases.json` file in the root directory of the wrapdb repository to include
    the new version of the wrap you are testing. This file is used to track the
    versions of the wraps available in the wrap database.


## Phase 4: Submit Your Changes

Once you have confirmed your updated wrap works locally, you can commit the
changes in your `wrapdb` clone and open a Pull Request to `mesonbuild/wrapdb` on
GitHub.
