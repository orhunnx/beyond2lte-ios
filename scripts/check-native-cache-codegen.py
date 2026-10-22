#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Verify compiler flags and actual scalar-only kernel cache machine code."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('object_root',type=Path,help='DEVELOPMENT_ARM64_B2L object directory')
p.add_argument('--objdump',default='llvm-objdump')
a=p.parse_args()
objects=[a.object_root/'bsd/DEVELOPMENT'/f'{x}.o' for x in ['b2l_native_cache','b2l_cache_core','CacheHfs','CacheSha']]
objects.append(a.object_root/'osfmk/DEVELOPMENT/b2l_arm64e_compat.o')
objects.append(a.object_root/'iokit/DEVELOPMENT/B2LCPU.cpo')
for obj in objects:
 args=json.loads(Path(str(obj)+'.json').read_text().rstrip(',\n'))['arguments']
 if '-mgeneral-regs-only' not in args:raise SystemExit(f'{obj.name}: scalar compiler flag missing')
 text=subprocess.check_output([a.objdump,'-d',str(obj)],text=True)
 if obj.name=="b2l_arm64e_compat.o" and "<_b2l_cache_resolve>:" not in text:raise SystemExit("VM resolver absent from scalar ARM64e object")
 for line in text.splitlines():
  if not re.match(r'\s*[a-f0-9]+:\s+[a-f0-9]+\s+',line):continue
  instruction=line.split('\t')[-1].split(';')[0]
  if re.search(r'\b(?:[bhsdq][0-9]+|v[0-9]+(?:\.[a-z0-9]+)?)\b',instruction):raise SystemExit(f'{obj.name}: unexpected SIMD/FP: {line.strip()}')
 print(f'{obj.name}: scalar compiler flag and machine code verified')
