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

"""Exercise the production native-worker queue and volatile PHY validation with UBSan."""
from pathlib import Path
import subprocess,tempfile,os,shlex
ROOT=Path(__file__).resolve().parents[2]
def function(s, signature):
 a=s.index(signature);b=s.index('{',a);n=1;c=b+1
 while n:n+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
def main():
 source=(ROOT/'firmware_p4/components/Applications/LoRa/session/lora_session.c').read_text()
 signatures=['static esp_err_t queue_text(', 'esp_err_t lora_session_send_text(',
 'esp_err_t lora_session_send_direct(', 'esp_err_t lora_session_advertise(',
 'void lora_session_process_tx(', 'esp_err_t lora_session_start_radio_owned(',
 'void lora_session_apply_start_radio(']
 header=(ROOT/'firmware_p4/components/Applications/LoRa/session/include/lora_session.h').read_text()
 end=header.index('} lora_tx_status_t;')+len('} lora_tx_status_t;')
 start=header.rfind('typedef struct {',0,end)
 production=header[start:end]+'\nstatic lora_tx_status_t s_tx_status;\n'+'\n'.join(function(source,x) for x in signatures)
 test=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',production)
 with tempfile.TemporaryDirectory(prefix='mesh-queue-') as tmp:
  p=Path(tmp);(p/'test.c').write_text(test)
  subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=gnu11','-Wall','-Wextra','-Werror',
   '-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True,timeout=15)
if __name__=='__main__':main()
