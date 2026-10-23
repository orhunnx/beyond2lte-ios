#!/usr/bin/env python3
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'scripts/decode-b2l-diag.py'
s=importlib.util.spec_from_file_location('diag',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
text="""B2L IPC: seq=1 dir=rx role=2 pid=72 tid=9 id=0x10000000 bytes=80
B2L IPCDATA: seq=1 part=ool captured=16
B2L IPCDATA: seq=1 part=ool off=0 hex=636f6e7461696e65722d6572726f7200
B2L IPCDATA: seq=2 part=inline captured=80
B2L IPCDATA: seq=2 part=inline off=64 hex=61626364
B2L IPCDATA: seq=2 part=inline off=68 copyin_error=1
"""
r=m.decode(text);assert r['messages'][0]['parts']['ool']['printable_tokens']==['container-error']
assert not r['messages'][0]['parts']['ool']['gaps']
assert r['messages'][1]['parts']['inline']['gaps']==[[0,64],[68,80]]
assert r['messages'][1]['parts']['inline']['copy_errors']==[68]
print('PASS IPC decoder: exact bytes/tokens, missing ranges and copy faults')
