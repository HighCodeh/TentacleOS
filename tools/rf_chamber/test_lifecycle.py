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

"""Fault-inject production SX1262 CW/stop and persistent ownership cleanup."""
from pathlib import Path
import os, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def extract(text, signature):
    start=text.index(signature);i=text.index('{',start);depth=1;j=i+1
    while depth:
        depth+=(text[j]=='{')-(text[j]=='}');j+=1
    return text[start:j]
def main():
    radio=(ROOT/'firmware_p4/components/Drivers/sx1262/sx1262_radio.c').read_text()
    service=(ROOT/'firmware_p4/components/Service/rf_chamber/rf_chamber.c').read_text()
    code='\n'.join(extract(radio,x) for x in ['esp_err_t sx1262_radio_standby_checked(void) {','esp_err_t sx1262_radio_lab_cw_start(void) {'])
    code+='\n'+extract(service,'static esp_err_t cleanup(rf_chamber_radio_t radio) {')
    fixture=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',code)
    with tempfile.TemporaryDirectory(prefix='rf-life-') as tmp:
        p=Path(tmp);(p/'test.c').write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True,timeout=30)
if __name__=='__main__':main()
