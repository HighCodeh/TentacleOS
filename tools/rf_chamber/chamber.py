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

"""Run bounded High Boy chamber tests; default matrix generation does not transmit."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import time

NOTICE = '\n'.join(line[2:] if line.startswith('# ') else '' for line in Path(__file__).read_text().splitlines()[0:15])
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
FIELDS = ('radio','mode','hz','dbm','path','sf','bw','cr','duration_ms','interval_ms')

def save(path, data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n' if not isinstance(data,str) else data)
    path.with_name(path.name+'.license').write_text(NOTICE+'\n')

def matrix():
    plan=[]
    def add(radio,mode,hz,dbm,path=0,sf=7,bw=125,cr=1,ms=1000,interval=10000):
        plan.append(dict(zip(FIELDS,(radio,mode,hz,dbm,path,sf,bw,cr,ms,interval))))
    for hz in (315000000,433920000,868350000,915000000):
        for path in (1,2,3,4):
            for dbm in (-30,-20,-10,0,5,10):add('cc','cw',hz,dbm,path)
            add('cc','rx',hz,-30,path,ms=10000)
            add('cc','tx',hz,0,path,ms=3000,interval=500)
    for hz in (868100000,868350000,869525000,902000000,915000000,928000000):
        for dbm in (-9,0,10,14,17,20,22):add('sx','cw',hz,dbm)
    for hz in (868100000,915000000):
        for sf in (7,9,12):
            for bw in (125,250,500):
                add('sx','tx',hz,0,sf=sf,bw=bw,ms=10000,interval=10000)
                add('sx','rx',hz,-9,sf=sf,bw=bw,ms=10000)
    return {'schema':1,'purpose':'laboratory plan; not measured results','cases':plan}

class Link:
    def __init__(self,port):
        import serial
        from serial.tools import list_ports
        ports=[p for p in list_ports.comports() if p.device==port]
        if not ports or ports[0].vid!=0x10c4 or ports[0].pid!=0xea70 or not port.endswith('0'):
            raise RuntimeError('Choose the High Boy P4 CP2105 port ending in 0; no automatic port selection.')
        self.serial_info={k:getattr(ports[0],k,None) for k in ('device','serial_number','vid','pid','description')}
        self.u=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=2,exclusive=True)
        self.u.dtr=False;self.u.rts=False;self.u.port=port;self.u.open();self.raw=[]
        self.read(.3)
    def read(self,seconds):
        end=time.monotonic()+seconds;chunks=[]
        while time.monotonic()<end:
            b=self.u.read(max(1,self.u.in_waiting))
            if b:chunks.append(b.decode('utf8','replace'))
        text=ANSI.sub('', ''.join(chunks));self.raw.append(text)
        if any(x in text for x in ('Guru Meditation','ESP-ROM:','Brownout detector')):
            raise RuntimeError('Device restarted or reported a fatal fault; RF state needs verification.')
        return text
    def command(self,text,wait=.5):
        self.raw.append('\nHOST> '+text+'\n');self.u.write((text+'\r').encode())
        return self.read(wait)
    def status(self):
        text=self.command('rfbench status')
        found=re.findall(r'STATUS (\{[^\r\n]+\})',text)
        if not found:raise RuntimeError('No rfbench status response')
        return json.loads(found[-1])
    def stop(self):
        self.command('rfbench stop')
        for _ in range(15):
            s=self.status()
            if s['phase']==0 and s['cleanup_error']==0:return s
            if s['phase']==4:raise RuntimeError('Cleanup failed: radio ownership retained; do not assume RF off.')
            time.sleep(.25)
        raise RuntimeError('Stop not confirmed')
    def close(self):self.u.close()

def run_case(link,case):
    before=link.status()
    if before['phase']!=0:raise RuntimeError('Radio bench is busy; stop it explicitly first.')
    command='rfbench '+' '.join(str(case[k]) for k in FIELDS)+' ARMED'
    response=link.command(command)
    if 'REQUEST ESP_OK' not in response:raise RuntimeError('Request rejected: '+response[-300:])
    run_id=before['id']+1;end=time.monotonic()+case['duration_ms']/1000+20;samples=[]
    while time.monotonic()<end:
        s=link.status();samples.append(s)
        if s['id']!=run_id:raise RuntimeError('Run ID changed; another client or a restart interfered.')
        if s['phase'] in (0,4):
            result={'requested':case,'snapshots':samples,'final':s}
            result['passed_control_checks']=s['phase']==0 and s['error']==0 and s['cleanup_error']==0
            return result
        time.sleep(.5)
    raise RuntimeError('Run deadline exceeded')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port');parser.add_argument('--output',default='rf_session.json')
    sub=parser.add_subparsers(dest='action',required=True)
    sub.add_parser('matrix');sub.add_parser('status');sub.add_parser('stop')
    single=sub.add_parser('run')
    single.add_argument('--radio',choices=('cc','sx'),required=True)
    single.add_argument('--mode',choices=('cw','tx','rx'),required=True)
    single.add_argument('--hz',type=int,required=True);single.add_argument('--dbm',type=int,default=-9)
    single.add_argument('--path',type=int,default=0);single.add_argument('--sf',type=int,default=7)
    single.add_argument('--bw',type=int,default=125);single.add_argument('--cr',type=int,default=1)
    single.add_argument('--duration-ms',type=int,default=1000);single.add_argument('--interval-ms',type=int,default=10000)
    single.add_argument('--armed-lab',action='store_true')
    batch=sub.add_parser('execute');batch.add_argument('plan');batch.add_argument('--armed-lab',action='store_true')
    batch.add_argument('--first',type=int,default=0);batch.add_argument('--count',type=int,default=1)
    args=parser.parse_args()
    if args.action=='matrix':
        plan=matrix();save(args.output,plan);print(len(plan['cases']),'planned cases; no USB access or transmission');return
    if not args.port:parser.error('--port is required')
    if args.action=='run':cases=[{k:getattr(args,k) for k in FIELDS}]
    elif args.action=='execute':
        plan=json.loads(Path(args.plan).read_text());cases=plan['cases'][args.first:args.first+args.count]
        if not cases:parser.error('Empty plan selection')
    else:cases=[]
    if any(c['mode']!='rx' for c in cases) and not args.armed_lab:
        parser.error('CW/TX requires --armed-lab and the laboratory measurement setup')
    result={'schema':1,'utc':dt.datetime.now(dt.timezone.utc).isoformat(),'cases':[],
            'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    link=None
    try:
        link=Link(args.port);result['usb']=link.serial_info
        result['firmware']=link.command('rfbench info')
        if args.action=='stop':result['final']=link.stop()
        elif args.action=='status':result['final']=link.status()
        else:
            result['battery_before']=link.command('battery')
            for case in cases:
                row=run_case(link,case);result['cases'].append(row);save(args.output,result)
                print(json.dumps(row['final']))
                if not row['passed_control_checks']:raise RuntimeError('Control checks failed; matrix stopped.')
                time.sleep(1)
            result['battery_after']=link.command('battery')
        result['status']='complete'
    except BaseException as exc:
        result.update(status='failed',error=str(exc))
        if link:
            try:result['stop_after_error']=link.stop()
            except Exception as stop_error:result['stop_error']=str(stop_error)
        raise
    finally:
        if link:
            save(str(args.output)+'.log',''.join(link.raw));link.close()
        save(args.output,result)
if __name__=='__main__':main()
