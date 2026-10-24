#!/usr/bin/env python3
"""Audit linked kernel code for RCpc instructions unavailable on the M4.
Pass --require-zero to fail the build gate if any remain.
"""
from pathlib import Path
import subprocess,re,json,sys
p=subprocess.Popen(['/opt/homebrew/opt/llvm/bin/llvm-objdump','-d',sys.argv[1]],stdout=subprocess.PIPE,text=True)
count=0;examples=[];symbol=''
for line in p.stdout:
 if re.match(r'^[0-9a-f]+ <.+>:',line):symbol=line.strip()
 if re.search(r'\s(?:ldapr\w*|ldapur\w*)\s',line):
  count+=1
  if len(examples)<12:examples.append({'symbol':symbol,'instruction':line.strip()})
assert p.wait()==0
print(json.dumps({'file':str(Path(sys.argv[1]).name),'rcpc_instructions':count,'examples':examples},indent=2))
if len(sys.argv)>2 and sys.argv[2]=='--require-zero':assert count==0,'RCpc remains in kernel text'
