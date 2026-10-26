#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Generate the pinned exFAT override without changing the SDK or downloading code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH_DIR = ROOT / "firmware_p4/components/Service/fatfs"
MARKER = ".tentacleos-generated"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked_path(root, name):
    path = Path(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Unsafe manifest path: " + name)
    result = root / path
    if result.is_symlink() or not result.resolve().is_relative_to(root.resolve()):
        raise ValueError("Manifest source escapes component: " + name)
    return result


def prepare(idf, output, patch_dir=PATCH_DIR):
    idf, output = idf.resolve(), output.resolve()
    source = idf / "components/fatfs"
    if output.is_relative_to(idf) or output.is_relative_to(patch_dir.resolve()):
        raise ValueError("Generated output must not overwrite SDK or tracked patch sources")
    if output.parts[-3:] != ("components", "Service", "fatfs"):
        raise ValueError("Output must be nested under components/Service/fatfs in a build directory")
    manifest = json.loads((patch_dir / "patch_manifest.json").read_text())
    if manifest.get("schema_version") != 1:
        raise ValueError("Unsupported FatFS manifest")
    if digest((idf / "tools/cmake/version.cmake").read_bytes()) != manifest["sdk_version_sha256"]:
        raise ValueError("FatFS override requires the reviewed ESP-IDF " + manifest["idf_version"])
    patch = (patch_dir / "exfat.patch").read_bytes()
    if digest(patch) != manifest["patch_sha256"]:
        raise ValueError("FatFS patch differs from reviewed manifest")
    contents = {}
    for name, expected in manifest["files"].items():
        contents[name] = checked_path(source, name).read_bytes()
        if digest(contents[name]) != expected:
            raise ValueError("SDK FatFS source differs from reviewed manifest: " + name)
    expected = dict(manifest["files"])
    if not manifest["patched_files"].keys() <= expected.keys():
        raise ValueError("Patch declares an unknown source")
    expected.update(manifest["patched_files"])
    notice = (patch_dir / "patch_manifest.json.license").read_text()
    marker = notice + "\nGenerated FatFS override; edit the source patch, not this directory.\n"
    if output.exists():
        if not (output / MARKER).is_file() or (output / MARKER).read_text() != marker:
            raise ValueError("Refusing to replace an unrecognized output directory")
        actual_files = {p.relative_to(output).as_posix() for p in output.rglob("*") if p.is_file()}
        if actual_files == set(expected) | {MARKER} and all(
            digest(checked_path(output, name).read_bytes()) == value for name, value in expected.items()
        ):
            return output
    # Apply outside any checkout so git apply cannot interpret repository-relative paths.
    with tempfile.TemporaryDirectory(prefix="tos-fatfs-") as temporary:
        work = Path(temporary) / "component"
        work.mkdir()
        for name, data in contents.items():
            target = checked_path(work, name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        env["GIT_CEILING_DIRECTORIES"] = str(work.parent)
        subprocess.run(["git", "apply", "--check", "--whitespace=nowarn", "-"],
                       cwd=work, env=env, input=patch, check=True, capture_output=True)
        subprocess.run(["git", "apply", "--whitespace=nowarn", "-"],
                       cwd=work, env=env, input=patch, check=True, capture_output=True)
        actual_files = {p.relative_to(work).as_posix() for p in work.rglob("*") if p.is_file()}
        if actual_files != set(expected):
            raise ValueError("Unexpected files after patch application")
        for name, value in expected.items():
            if digest(checked_path(work, name).read_bytes()) != value:
                raise ValueError("Patched output differs from reviewed manifest: " + name)
        (work / MARKER).write_text(marker)
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".fatfs-stage-", dir=output.parent) as staging:
            candidate = Path(staging) / "fatfs"
            shutil.copytree(work, candidate)
            previous = Path(staging) / "previous"
            if output.exists():
                output.rename(previous)
            try:
                candidate.rename(output)
            except OSError:
                if previous.exists():
                    previous.rename(output)
                raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.idf, args.out)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, "FatFS generation failed: " + str(error) + "\n")
    print("Pinned ESP-IDF v6.1 exFAT override ready: " + str(args.out))


if __name__ == "__main__":
    main()
