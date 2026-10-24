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
 assert images[0]==images[1] and len(images[0])==152
 insns=struct.unpack('<8I',images[0][:32])
 # ADR x17 points to stage at48; LDR literal x16 points to full64-bit target at40.
 assert insns[1]&31==17
 adr_imm=((insns[1]>>29)&3)|(((insns[1]>>5)&0x7ffff)<<2)
 assert 4+adr_imm==48
 assert insns[6]&31==16 and 24+(((insns[6]>>5)&0x7ffff)<<2)==40
 assert insns[7]==0x14000009 # Branch to capture at64, keeping header offsets.
 assert struct.unpack_from('<I',images[0],120)[0]==0xd61f0200 # Full64-bit BR x16 after capture.
 assert images[0][128:]==bytes(24) # Raw EL/MPIDR/MIDR output slots.
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  '-I'+str(r/'common/smp'),str(r/'tests/secondary_entry_test.c'),str(r/'uefi/XnuLoader/Arena.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'elf.bin')],check=True)
 subprocess.run(['python3',str(r/'tests/test-loader-secondary.py'),str(p/'elf.bin')],check=True)

 harness=r"""
.text
.global _start
_start:
 adrp x20,B2LSecondaryTemplate
 add x20,x20,:lo12:B2LSecondaryTemplate
 adr x21,done
 str x21,[x20,#40]
 ldr x0,=0x1122334455667788
 br x20
done:
 ldr x1,=0x1122334455667788
 cmp x0,x1
 b.ne fail
 ldr x1,[x20,#48]
 cmp x1,#2
 b.ne fail
 ldr x1,[x20,#128]
 mrs x2,CurrentEL
 cmp x1,x2
 b.ne fail
 ldr x1,[x20,#136]
 mrs x2,MPIDR_EL1
 cmp x1,x2
 b.ne fail
 ldr x1,[x20,#144]
 mrs x2,MIDR_EL1
 cmp x1,x2
 b.ne fail
 mov x2,#0
 b exit
fail:
 mov x2,#1
exit:
 adr x1,exit_block
 str x2,[x1,#8]
 mov x0,#0x20
 hlt #0xf000
 b .
.align 3
exit_block: .quad 0x20026,0
"""
 (p/'harness.S').write_text(harness)
 subprocess.run([clang,'--target=aarch64-none-elf','-c',str(p/'harness.S'),'-o',str(p/'harness.o')],check=True)
 subprocess.run(['/opt/homebrew/bin/ld.lld','-e','_start','-Ttext=0x40080000',str(p/'harness.o'),str(p/'elf.o'),'-o',str(p/'run.elf')],check=True)
 subprocess.run(['qemu-system-aarch64','-machine','virt','-cpu','cortex-a53','-m','128M','-nographic','-semihosting-config','enable=on,target=native','-kernel',str(p/'run.elf')],check=True,timeout=20)
print('ELF/UEFI COFF identity, offsets, full64-bit branch, register capture and preserved x0 in QEMU PASS')
