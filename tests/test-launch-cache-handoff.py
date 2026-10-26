#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Reproduce the native parent/child shared-memory handoff and validate padding."""
from pathlib import Path
import importlib.util,plistlib,struct
r=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cache',r/'scripts/b2l_launch_cache.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
original=plistlib.dumps({'LaunchDaemons':{'original':{'Label':'original','ProgramArguments':['/original']}},'OriginalData':b'X'*4096},fmt=plistlib.FMT_BINARY)
# A compact writer can generate a shorter file even when one job is added.
old=plistlib.loads(original);old['OriginalData']=b'X'*4096
new=dict(old);new['LaunchDaemons']=dict(old['LaunchDaemons']);new['LaunchDaemons']['owned']={'Label':'owned'}
compact=plistlib.dumps(new,fmt=plistlib.FMT_BINARY)
expected=max(len(original),len(compact))+2048
# Parent retains a fixed logical length; bytes after the shorter child copy
# remain dirty. The trailer at that fixed end must describe the replacement.
parent=bytearray(b'Q'*expected);parent[:len(compact)]=compact
try:plistlib.loads(parent)
except Exception:pass
else:raise AssertionError('Short handoff unexpectedly parsed')
padded=m.pad_binary_plist(compact,expected);parent[:len(padded)]=padded
assert plistlib.loads(parent)==new and len(parent)==expected
assert m.pad_binary_plist(compact,len(compact))==compact
for target in [len(compact)-1,4*1024*1024+1]:
 try:m.pad_binary_plist(compact,target)
 except ValueError:pass
 else:raise AssertionError('Invalid target accepted')
for bad in [b'',b'notaplist',compact[:-16],compact[:-8]+b'\xff'*8]:
 try:m.pad_binary_plist(bad,expected)
 except ValueError:pass
 else:raise AssertionError('Invalid plist accepted')
print('native shared-handoff model: short-copy failure reproduced; exact-length trailer, semantics and bounds PASS')
