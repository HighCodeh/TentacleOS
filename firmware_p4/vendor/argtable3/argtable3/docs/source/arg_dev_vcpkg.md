# Maintain vcpkg

[**vcpkg**](https://vcpkg.io) is a free, open-source, cross-platform C/C++
package manager by Microsoft that simplifies acquiring and managing third-party
libraries. It automates downloading, building, and installing dependencies,
helping developers streamline their workflow and easily integrate libraries into
their projects.

This document outlines the steps required to update the Argtable3 port in the
vcpkg registry after a new version of the library has been released. Following
these instructions will ensure that vcpkg users can access the latest features
and fixes of Argtable3.

## Phase 1: Preparation and Local Changes

1. **Fork and Clone the vcpkg Repository:**
    * If you haven't already, fork the `microsoft/vcpkg` repository on GitHub to your own account.
    * Clone your fork locally:
        ```bash
        git clone https://github.com/YOUR_USERNAME/vcpkg.git
        cd vcpkg
        ```
    * Ensure your vcpkg instance is bootstrapped:
        ```bash
        ./bootstrap-vcpkg.sh -disableMetrics  # Linux/macOS
        # or
        .\bootstrap-vcpkg.bat -disableMetrics # Windows
        ```

2. **Create a New Branch:** It's best practice to make your changes on a new
    branch:
    ```bash
    git checkout -b update-argtable3-to-NEW_VERSION
    # Example: git checkout -b update-argtable3-to-3.3.0
    ```

3. **Locate Your Port Files:** The files for your `argtable3` port will be in
    the `ports/argtable3/` directory. The main files you'll likely interact with
    are:
    * `vcpkg.json`: The manifest file containing metadata for the port, such as
      its version, description, and dependencies. While `vcpkg.json` specifies
      the library version, the SHA512 hash used to verify the downloaded source
      code archive is not stored in this file. Instead, this hash is typically
      found in the `portfile.cmake` (e.g., as the `SHA512` argument to functions
      like `vcpkg_from_github` or `vcpkg_download_distfile`) and is also
      recorded in vcpkg's version database.
    * `portfile.cmake`: The CMake script that defines how to download, build,
      and install your library.

4. **Update `ports/argtable3/vcpkg.json`:**
    * Change the `"version"` field to your new library's release version (e.g., `"3.3.0"`).
    * After updating the main `"version"` field, check if the previous
      `vcpkg.json` also contained a `"port-version"` field (e.g., `port-version:
      1`, which would correspond to a version like `3.0.0#1`). If it did, you
      should remove this `"port-version"` field or set its value to `0` for the
      new upstream version. For example, when updating to upstream version
      `3.1.0`, the `vcpkg.json` should initially only define `version: "3.1.0"`.
      A `port-version` (resulting in suffixes like `#1`, `#2`, etc.) is only
      introduced or incremented if you later make changes to the vcpkg port
      files (such as `portfile.cmake` or patches) while keeping the main
      `"version"` (e.g., `3.1.0`) the same.

5. **Update `ports/argtable3/portfile.cmake`:**
    * **Source Code Location:** Ensure the URL points to the new release archive in `vcpkg_download_distfile`.
    * **Build Process:** Review the build commands (e.g., `vcpkg_cmake_configure`, `vcpkg_cmake_install`) to ensure they are still correct for the new version. Sometimes build options change.

6.  **Update Version Information & Calculate Source Code Hash (SHA512):**
    This is a **critical** step. vcpkg verifies the integrity of downloaded files using SHA512 hashes.
    * **Recommended Method: `vcpkg x-add-version`**
        This command automates updating the version database and calculating the hash.
        ```bash
        ./vcpkg x-add-version argtable3
        ```
        This command will:
        1.  Read the version from `ports/argtable3/vcpkg.json`.
        2.  Attempt to download the source archive based on the information in `ports/argtable3/portfile.cmake` (e.g., using `vcpkg_from_github`).
        3.  Calculate the SHA512 hash of the downloaded archive.
        4.  Update the `SHA512` field in `portfile.cmake` (if using `vcpkg_from_github` or similar that takes a `SHA512` argument).
        5.  Create/update an entry in the version database file (e.g., `versions/a-/argtable3.json`). This file links the library version to a specific Git commit of the `ports/argtable3` directory.

    * **Manual Method (if `x-add-version` fails or for understanding):**
        1.  Manually download the source archive (e.g., `argtable3-NEW_VERSION.tar.gz` or `.zip`) for your new release.
        2.  Calculate its SHA512 hash:
            * Linux: `sha512sum argtable3-NEW_VERSION.tar.gz`
            * macOS: `shasum -a 512 argtable3-NEW_VERSION.tar.gz`
            * PowerShell (Windows): `Get-FileHash argtable3-NEW_VERSION.tar.gz -Algorithm SHA512 | Select-Object -ExpandProperty Hash`
        3.  Open `ports/argtable3/portfile.cmake` and update the `SHA512` argument in `vcpkg_from_github` (or the equivalent in other download functions) with this new hash.
        4.  After manually setting the hash, you would still typically run `vcpkg x-add-version argtable3` to update the version database, possibly with `--skip-download` if you are certain about the hash and download mechanism.

7.  **Format Manifest Files:**
    Ensure your `vcpkg.json` file is correctly formatted:
    ```bash
    ./vcpkg format-manifest ports/argtable3/vcpkg.json
    ```

## **Phase 2: Testing**

8.  **Test the Port Locally:**
    * Remove any old installed version of `argtable3` to ensure a clean test:
        ```bash
        ./vcpkg remove argtable3
        ```
      Or you can remove all installed ports if you want to start fresh:
        ```bash
        rm -rf buildtrees
        rm -rf downloads
        rm -rf installed
        rm -rf packages
        ```

    * Install your modified port for all configurations you want to test:
        ```bash
        ./vcpkg install argtable3 --triplet x64-linux
        # Or for Windows:
        ./vcpkg install argtable3 --triplet x64-windows
        ./vcpkg install argtable3 --triplet x64-windows-release
        ./vcpkg install argtable3 --triplet x64-windows-static
        ./vcpkg install argtable3 --triplet x64-windows-static-md
        # Or for macOS:
        ./vcpkg install argtable3 --triplet x64-osx
        ```
    * Examine the build logs for errors. Verify that the correct version of the
      library was downloaded and installed.
    * Build a small test application that links against the newly installed
      `argtable3` to ensure it works. You can try
      [vcpkg-test.tar.gz](/_static/vcpkg-test.tar.gz) on Linux and macOS, or you
      can try [vcpkg-test.zip](/_static/vcpkg-test.zip) on Windows. When you
      build the sample application, ensure you specify the
      `CMAKE_TOOLCHAIN_FILE` to point to your vcpkg instance:
        ```bash
        make VCPKG_TOOLCHAIN=/path/to/your/testing_vcpkg_repo/scripts/buildsystems/vcpkg.cmake
        ```
        ```{note}
        **How to ensure your testing vcpkg repo is used:**

        * The `CMAKE_TOOLCHAIN_FILE` setting forces CMake to use the `vcpkg.cmake` script from your specified vcpkg instance.
        * This `vcpkg.cmake` script modifies how `find_package()` works. When your `CMakeLists.txt` calls `find_package(argtable3 ...)`:
            * It consults the vcpkg installation specified by the toolchain file.
            * It looks for `argtable3` within that vcpkg instance's `installed` directory for the target triplet.
            * It sets up the include paths, library paths, and imported targets (`argtable3::argtable3`) based on what's found in *that specific vcpkg instance*.

        This method isolates your sample project's dependencies to your testing vcpkg environment, effectively bypassing any other vcpkg instances or system-wide libraries for the packages managed by vcpkg.
        ```

## **Phase 3: Submitting the Update**

9.  **Commit Your Changes:**
    Add all the modified files. This will usually include:
    * `ports/argtable3/vcpkg.json`
    * `ports/argtable3/portfile.cmake`
    * The corresponding file in the `versions/` directory (e.g., `versions/a-/argtable3.json`)
    ```bash
    git add ports/argtable3/ versions/a-/argtable3.json
    git commit -m "[argtable3] Update to version NEW_VERSION"
    # Example: git commit -m "[argtable3] Update to version 3.3.0"
    ```

10. **Push to Your Fork:**
    ```bash
    git push -u origin update-argtable3-to-NEW_VERSION
    ```

11. **Create a Pull Request (PR):**
    * Go to your fork on GitHub (`https://github.com/YOUR_USERNAME/vcpkg`).
    * You should see a button to "Compare & pull request" for your new branch. Click it.
    * **Title:** Make it clear, like `[argtable3] Update to version X.Y.Z`.
    * **Description:**
        * Briefly explain the update.
        * **Link to your library's release notes for the new version.** This is very helpful for maintainers.
        * You can use vcpkg's helper script to generate a template for your PR message:
            ```bash
            ./vcpkg x-generate-default-port-notes argtable3 NEW_VERSION > pr_notes.txt
            ```
            Then copy the content of `pr_notes.txt` into your PR description.
        * Mention any significant changes you made to the portfile or patches.
    * Ensure the base repository is `microsoft/vcpkg` and base branch is `master`.
    * Submit the PR.

12. **Address Feedback and CI:**
    * The vcpkg CI (Continuous Integration) will automatically test your changes on various platforms.
    * Maintainers will review your PR. Be responsive to any feedback or requests for changes.

By following these steps, you can effectively update your `argtable3` port in vcpkg! Good luck!
