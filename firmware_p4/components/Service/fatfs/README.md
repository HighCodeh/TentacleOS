<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# FatFS patch metadata

This directory holds the reviewed exFAT patch and its input/output hashes, not
a vendored component. CMake generates the `fatfs` override inside the selected
build directory. See [the FatFS documentation](../../../../docs/fatfs/README.md)
for the preserved adaptations, generation process, tests and update procedure.
