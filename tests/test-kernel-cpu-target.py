#!/usr/bin/env python3
"""Reject implicit Apple CPU features in the B2L kernel compiler flags."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'out/build-env-0063/src/xnu/makedefs/MakeInc.def').read_text()
flags=re.search(r'^MACHINE_FLAGS_ARM64_B2L\s*=\s*(.+)$',s,re.M).group(1).split()
clang='/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
probe='''unsigned a32(const unsigned*p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
unsigned long long a64(const unsigned long long*p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
unsigned add(unsigned*p){return __atomic_fetch_add(p,1,__ATOMIC_ACQ_REL);}
unsigned long long pan(void){return __builtin_arm_rsr64("pan");}
'''
with tempfile.TemporaryDirectory(prefix='b2l-cpu-target-') as td:
 p=Path(td)
 for language,suffix in [('c','.c'),('c++','.cpp')]:
  src=p/('probe'+suffix);src.write_text(probe);asm=p/(language+'.s')
  cmd=[clang,'--target=arm64-apple-ios26.5.0',*flags,'-O2','-ffreestanding','-mgeneral-regs-only','-S',str(src),'-o',str(asm)]
  subprocess.run(cmd,check=True)
  code=asm.read_text().lower()
  assert re.search(r'\bldar\s+w',code) and re.search(r'\bldar\s+x',code),code
  assert re.search(r'\bmrs\s+x\d+,\s*pan',code),code
  assert not re.search(r'\b(?:ldapr\w*|ldapur\w*|cas\w*|ldadd\w*|swp\w*)\s',code),code
  driver=subprocess.run(cmd+['-###'],capture_output=True,text=True,check=True).stderr
  assert '"-target-cpu" "generic"' in driver,driver
print('Actual B2L C/C++ flags: explicit generic CPU, PAN retained, LDAR32/64 and baseline exclusive RMW PASS')
