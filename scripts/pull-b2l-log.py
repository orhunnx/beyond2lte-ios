#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Read the B2L SYSTEM log tail from recovery and decode it; never write a device."""
import argparse,json,re,shlex,subprocess,sys
from pathlib import Path
REGION=16*1024*1024

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);p.add_argument('--output',required=True,type=Path);p.add_argument('--adb',default='adb');a=p.parse_args()
 adb=[a.adb,'-s',a.serial]
 def shell(command):return subprocess.check_output(adb+['shell','-T',command],text=True).strip()
 if subprocess.check_output(adb+['get-state'],text=True).strip()!='recovery':p.error('device must be in recovery')
 if shell('getprop ro.product.device')!='beyond2lte' or shell('id -u')!='0':p.error('expected rooted beyond2lte recovery')
 dev=shell('readlink -f /dev/block/by-name/system')
 if not re.fullmatch(r'/dev/block/sd[a-z][0-9]+',dev):p.error('unexpected SYSTEM block path')
 size=int(shell('blockdev --getsize64 '+shlex.quote(dev)))
 if size<=REGION+1024*1024 or size%4096:p.error('invalid SYSTEM geometry')
 raw=a.output.with_suffix('.region.bin')
 if a.output.exists() or raw.exists():p.error('output already exists; choose a new path')
 a.output.parent.mkdir(parents=True,exist_ok=True)
 command=f'dd if={shlex.quote(dev)} bs=4096 skip={(size-REGION)//4096} count={REGION//4096} 2>/dev/null'
 data=subprocess.check_output(adb+['exec-out','sh','-c',command])
 if len(data)!=REGION:p.error('short recovery read')
 with raw.open('xb') as f:f.write(data)
 subprocess.run([sys.executable,str(Path(__file__).with_name('b2l-log-region.py')),'decode',str(raw),'--text',str(a.output)],check=True)
 print(json.dumps({'raw_bytes':len(data),'text_bytes':a.output.stat().st_size,'device_writes':False}))

if __name__=='__main__':main()
