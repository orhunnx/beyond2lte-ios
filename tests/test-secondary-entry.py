#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Cross-assemble the actual low entry for ELF/UEFI COFF and test its ABI."""
from pathlib import Path
import struct
import subprocess
import tempfile
r=Path(__file__).resolve().parents[1]
clang='/opt/homebrew/opt/llvm/bin/clang'
objcopy='/opt/homebrew/opt/llvm/bin/llvm-objcopy'
with tempfile.TemporaryDirectory(prefix='b2l-secondary-') as td:
 p=Path(td);images=[]
 for kind,target in [('elf','aarch64-none-elf'),('coff','aarch64-unknown-windows-msvc')]:
  obj=p/(kind+'.o');binary=p/(kind+'.bin')
  subprocess.run([clang,'--target='+target,'-c',str(r/'uefi/XnuLoader/SecondaryEntry.S'),'-o',str(obj)],check=True)
  subprocess.run([objcopy,'--dump-section','.text='+str(binary),str(obj)],check=True)
  images.append(binary.read_bytes())
 assert images[0]==images[1] and len(images[0])==64
 insns=struct.unpack('<8I',images[0][:32])
 # ADR x17 points to stage at48; LDR literal x16 points to full64-bit target at40.
 assert insns[1]&31==17
 adr_imm=((insns[1]>>29)&3)|(((insns[1]>>5)&0x7ffff)<<2)
 assert 4+adr_imm==48
 assert insns[6]&31==16 and 24+(((insns[6]>>5)&0x7ffff)<<2)==40
 assert insns[7]==0xd61f0200 # BR x16, no stack/return/32-bit target truncation.
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  '-I'+str(r/'common/smp'),str(r/'tests/secondary_entry_test.c'),str(r/'uefi/XnuLoader/Arena.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'elf.bin')],check=True)
print('ELF and UEFI COFF low-entry code is identical; PC-relative offsets and full64-bit target verified')
