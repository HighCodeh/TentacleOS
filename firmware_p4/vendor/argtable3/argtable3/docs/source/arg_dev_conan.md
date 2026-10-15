# Maintain Conan Center Index

**Conan** is an open-source, decentralized, and multi-platform C/C++ package
manager that helps developers build applications and automate dependencies. It
allows for the creation and sharing of binary packages, simplifying the process
of integrating third-party libraries. The **Conan Center Index** is the official
central repository for Conan recipes, hosting a vast collection of open-source
C/C++ libraries. When a library like argtable3 is updated, its recipe in the
Conan Center Index needs to be updated to make the new version available to the
wider Conan community.

This document provides a step-by-step guide for updating the argtable3 recipe in
the Conan Center Index. It outlines the necessary procedures to ensure that when
a new version of the argtable3 library is released, Conan users can promptly
access the latest updates through the central repository.

## Phase 1: Setting Up Your Environment

First, you need to prepare your local environment to work on the recipe.

1. **Prerequisites:**
    * **Git:** You must have Git installed.
    * **Python & Conan:** You need a working Python installation and the Conan
      package manager. If you don't have Conan, install it with pip:
      ```bash
      python -m venv .venv
      .venv\Scripts\activate     # On Windows
      # or
      source .venv/bin/activate  # On Linux/macOS
      pip install conan
      ```

2. **Fork and Clone Conan-Center-Index:**
    * Go to the `conan-io/conan-center-index` repository on GitHub and create a
      fork under your own account.
    * Clone your fork to your local machine:
        ```bash
        git clone https://github.com/YOUR_USERNAME/conan-center-index.git
        cd conan-center-index
        ```

3. **Create a New Branch:**
    It's crucial to make your changes on a new, descriptively named branch.
    ```bash
    git checkout -b update-argtable3-to-3.3.0
    ```

## Phase 2: Updating the `argtable3` Recipe

Now you will modify the files that define the `argtable3` package.

4. **Locate the Recipe:** The recipe for `argtable3` is located in
    `recipes/argtable3`. Navigate to the `recipes/argtable3/all` directory.

5. **Update `conandata.yml`:** This file stores data that is separate from the
    build logic, primarily the source code locations and checksums.
    * Open the `conandata.yml` file.
    * You will see entries for previous versions under the `sources` key. Add a
      new entry for your new release (e.g., `3.3.0`).
    * This entry needs the direct `url` to your new release's source archive
      (tar.gz or zip) and its `sha256` hash.
        ```{note}
        **To get the SHA256 hash:**
        * Download the source archive for your new release.
        * Run one of the following commands:
            * **Linux:** `sha256sum argtable3-3.3.0.tar.gz`
            * **macOS:** `shasum -a 256 argtable3-3.3.0.tar.gz`
            * **PowerShell:** `Get-FileHash argtable3-3.3.0.tar.gz -Algorithm SHA256 | Select-Object -ExpandProperty Hash`
        ```


    **Example `conandata.yml` addition:**
    ```yaml
    sources:
      # ... existing versions ...
      "3.3.0":
        url: "https://github.com/argtable/argtable3/releases/download/v3.3.0.116da6c/argtable-v3.3.0.116da6c.tar.gz"
        sha256: "21d653eaab4b9dede76ceb0cb4e8878cf5cadb48ef9180c6a4d1a5cef1549e65"
    ```

6. **Update `config.yml`:** This file maps package versions to the folder
    containing the recipe that builds them. For a simple version update where
    the build process hasn't changed, you just need to add the new version.
    * Open the `config.yml` file.
    * Add your new version (`3.3.0`) to the `versions` map, pointing it to the
      same recipe folder as the previous version.

    **Example `config.yml` addition:**
    ```yaml
    versions:
      # ... existing versions ...
      "3.3.0":
        folder: "all" # Or whatever folder the latest recipe is in
    ```

7. **Review `conanfile.py`:** This is the main Python script that defines how
    to build the package. For a simple version bump where the build process,
    dependencies, and required patches have not changed, **you often do not need
    to modify this file at all.** The `conanfile.py` is usually written to be
    version-agnostic, pulling the version number and source URL from the
    metadata you just updated.

    However, you should quickly review it to see if anything needs to change, such as:
    * Minimum required compiler versions.
    * Build options or CMake definitions that might have changed in `argtable3`.
    * Any patches that were applied to previous versions. Check if they are
      still needed for the new version or if they have been merged upstream.
      Patches are also listed in `conandata.yml`.

## Phase 3: Testing Your Changes Locally

**This is a critical, mandatory step.** Before submitting a pull request, you
must verify that your updated recipe can build the package successfully on your
local machine.

8. **Navigate to the Recipe Directory:** Make sure you are in the directory for
    the version you are updating (e.g., `recipes/argtable3/all`).

9. **Run `conan create`:**
    This command builds the package and runs a small test to ensure it's usable.
    ```bash
    conan create . --version=3.3.0 --build=missing \
      -s build_type=Debug \
      -o shared=True \
      -s compiler.runtime=static
    ```
    * `conan create .`: Tells Conan to build the recipe in the current directory.
    * `--version=3.3.0`: Explicitly tells Conan which version you are building (as defined in `config.yml` and `conandata.yml`).
    * `--build=missing`: Instructs Conan to build any missing dependencies from source if pre-built binaries aren't available.
    * `-s build_type=Debug`: Specifies the build type (you can also test with `Release`).
    * `-o shared=True`: Indicates that you want to build a shared library.
    * `-s compiler.runtime=static`: Specifies that you want to build with the MSVC static runtime (/MT or /MTd).
    ```{note}
    You can test with `Makefile` in [conan-test.tar.gz](/_static/conan-test.tar.gz)
    on Linux or [conan-test.zip](_static/conan-test.zip) on Windows. This `Makefile`
    provides targets for building the `argtable3` package in various configurations,
    including debug and release builds, both shared and static libraries. It also
    includes a clean target to remove build artifacts. Extract `Makefile` to the
    `recipes/argtable3/all` directory and run:
    ```bash
    make
    # Or for a specific build type:
    make debug
    # Or for a specific version:
    make ARGTABLE3_VERSION=3.3.0
    ```

10. **Check the `test_package`:** The `conan create` command will automatically
    compile and run the code in the `test_package` subfolder. This test
    typically includes a small program that includes a header from your library
    and links against it. If this step passes, your local validation is likely
    successful.
    ```{note}
    You can also run `conan test` to execute the tests defined in the `test_package` directory:
    ```bash
    conan test /path/to/argtable3/test_package argtable3/3.3.0@
    ```

## Phase 4: Submitting to Conan-Center-Index

Once your local tests pass, you are ready to create a pull request.

11. **Commit and Push:**
    Commit your changes to your branch and push them to your fork on GitHub.
    ```bash
    git add .
    git commit -m "argtable3: Add version 3.3.0"
    git push -u origin update-argtable3-to-3.3.0
    ```

12. **Create a Pull Request (PR):**
    * Go to your fork on GitHub. You should see a prompt to create a PR from your new branch.
    * The PR should be targeted at the `master` branch of the `conan-io/conan-center-index` repository.
    * **Title:** Use the standard format: `argtable3: Add version 3.3.0`.
    * **Description:** When you open the PR, a bot will post a comment with a
      checklist. **Read this checklist carefully** and check off the boxes that
      apply (e.g., "I've tested this locally," "I've updated conandata.yml,"
      etc.).

13. **Monitor the CI (Continuous Integration):** After you create the PR, a
    complex CI system will automatically start building your package on numerous
    platforms (Windows, Linux, macOS), compilers, and configurations.
    * Monitor the results of these builds. If any fail, you will need to
      investigate the logs and push fixes to your branch.
    * Once all checks pass, a maintainer will review and merge your PR, and your
      new `argtable3` version will become available on Conan-Center.
