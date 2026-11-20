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

"""Compile the actual full-firmware capture reader with an explicit timestamp source."""
from pathlib import Path
import os, re, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
DRIVER=ROOT/'firmware_p4/components/Drivers/lf_rfid'
source=re.sub(r'^#include .*$', '', (DRIVER/'lf_rfid_reader.c').read_text(), flags=re.M)
fixture=Path(__file__).with_suffix('.c').read_text().replace('/* READER */',source)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n'+''.join('#define '+x+' '+str(i)+'\n' for i,x in enumerate(['ESP_OK','ESP_FAIL','ESP_ERR_INVALID_ARG','ESP_ERR_INVALID_STATE','ESP_ERR_NOT_FOUND','ESP_ERR_NO_MEM','ESP_ERR_INVALID_SIZE','ESP_ERR_NOT_SUPPORTED'])))
 (p/'test.c').write_text(fixture)
 subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(p),'-I'+str(DRIVER/'include'),str(p/'test.c'),str(DRIVER/'lf_rfid_decoder.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=20)
