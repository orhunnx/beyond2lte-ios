#!/usr/bin/env python3
"""Run actual Mach-O assembly vs C and independent frozen QARMA vectors.

ELF assembler accepts semicolon instruction separators; Darwin treats them as
comments. Keep this native-Darwin check in addition to the Cortex-A53 ELF test.
"""
from pathlib import Path
import subprocess,tempfile,platform
r=Path(__file__).resolve().parents[1]
if platform.system()!='Darwin' or platform.machine()!='arm64':
 raise SystemExit('Native Darwin arm64 required for this assembler regression')
s=r/'xnu-overlay/osfmk/arm64';t=r/'tests/arm64e/pacga'
with tempfile.TemporaryDirectory(prefix='b2l-qarma-darwin-') as td:
 p=Path(td)
 for f in ['test-pacga.c','test-vectors.inc']:(p/f).write_bytes((t/f).read_bytes())
 for f in ['b2l_pacga_qarma5.c','b2l_pacga_qarma5.h']:(p/f).write_bytes((s/f).read_bytes())
 subprocess.run(['clang','-c','-x','assembler-with-cpp',str(s/'b2l_pacga_qarma5_arm64.s'),'-o',str(p/'qarma.o')],check=True)
 subprocess.run(['clang','-O2','-DB2L_TEST_ASM','-mgeneral-regs-only','-Wall','-Wextra','-Werror',str(p/'test-pacga.c'),str(p/'qarma.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
 print('Darwin Mach-O production assembly agrees with C and all 1259 frozen QARMA/PACGA vectors')
