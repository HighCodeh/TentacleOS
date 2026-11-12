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
"""Exercise production orientation/calibration code; no hardware access."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
D=ROOT/'firmware_p4/components/Applications/ui/screens/games'
with tempfile.TemporaryDirectory(prefix='imu-orientation-') as temp:
 exe=Path(temp)/'test'
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                 '-I',str(D/'include'),str(ROOT/'tools/highboy/test_imu_orientation.c'),
                 str(D/'imu_orientation.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
