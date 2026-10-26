#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
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

"""Resolve host-test cJSON through the same manager and version as both firmwares."""
import os
from pathlib import Path
import subprocess
import sys

from ruamel.yaml import YAML

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(__file__).resolve().parent / "build"


def main():
    yaml = YAML(typ="safe")
    versions = [yaml.load((ROOT / f"firmware_{chip}/main/idf_component.yml").read_text())
                ["dependencies"]["espressif/cjson"] for chip in ("p4", "c5")]
    if versions[0] != versions[1]:
        raise SystemExit("Host tests require matching P4/C5 cJSON versions")
    main_dir = WORK / "main"
    main_dir.mkdir(parents=True, exist_ok=True)
    notice = (Path(__file__).read_text().split("\n\n", 1)[0].split("\n", 1)[1] + "\n\n")
    (main_dir / "CMakeLists.txt").write_text(notice + "idf_component_register()\n")
    (main_dir / "idf_component.yml").write_text(
        notice + f'dependencies:\n  espressif/cjson: "{versions[0]}"\n')
    # Dependency resolution needs version metadata; no SDK compilation occurs here.
    sdk = WORK / "resolver-sdk"
    sdk.mkdir(exist_ok=True)
    env = {**os.environ, "IDF_PATH": str(sdk), "IDF_VERSION": "6.1",
           "IDF_TARGET": "esp32p4", "IDF_COMPONENT_MANAGER": "1"}
    subprocess.run([sys.executable, "-m", "idf_component_manager.prepare_components",
                    "--project_dir", str(WORK), "prepare_dependencies",
                    "--managed_components_list_file", str(WORK / "components.cmake"),
                    "--build_dir", str(WORK)], env=env, check=True)
    print("Host cJSON prepared from firmware manifests; no device or SDK build used.")


if __name__ == "__main__":
    main()
