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

"""Verify the production ST25 register burst and inline SPI framing."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile
from test_emu_tx import ROOT,extract
hal=ROOT/'firmware_p4/components/Drivers/st25r3916/hal/hb_nfc_spi.c'
s=hal.read_text()
functions='\n'.join(extract(s,n) for n in ('esp_err_t hb_nfc_spi_reg_read(', 'esp_err_t hb_nfc_spi_reg_read_many(', 'esp_err_t hb_nfc_spi_reg_write(', 'esp_err_t hb_nfc_spi_direct_cmd('))
constants='\n'.join(re.findall(r'^#define HB_NFC_[^\n]+',s,re.M))
fixture=Path(__file__).with_suffix('.c').read_text().replace('/* CONSTANTS */',constants).replace('/* PRODUCTION */',functions)
with tempfile.TemporaryDirectory(prefix='nfc-spi-burst-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(hal.parent.parent/'include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
