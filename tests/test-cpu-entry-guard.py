#!/usr/bin/env python3
"""Run actual stackless identity macro and entry/reset wrappers under QEMU."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'out/build-env-0063/src/xnu/osfmk/arm64/start.s').read_text()
a=s.index('.macro B2L_CPU_INDEX');b=s.index('.macro B2L_SMP_STAGE',a)
macro=s[a:b].replace(r'mrs \logical, MPIDR_EL1',r'mov \logical,x0').replace(r'mrs \scratch, MIDR_EL1',r'mov \scratch,x2')
a=s.index('    /* Explicit EL1 affinity');b=s.index('Lb2l_part_accepted:',a)
guard=s[a:b]
a=s.index('    /* MIDR distinguishes');b=s.index('#elif HAS_CLUSTER',a)
reset=s[a:b]
asm='''#include "b2l_cpu_affinity.h"
.text
.align 2
'''+macro+'''
.global guard_test
guard_test:
 mov x2,x1
'''+guard+'''Lb2l_part_accepted:
 mov w0,#1
 ret
Lb2l_secondary_bad_affinity:
 mov x0,#-1
 ret
.global identity_test
identity_test:
 mov x2,x1
 B2L_CPU_INDEX x3,x4,Lb2l_secondary_bad_affinity
 mov x0,x3
 ret
.global reset_test
reset_test:
 mov x2,x1
'''+reset+'''
 ret
.global _start
_start:
 ldr x0,=0x48000000
 mov sp,x0
 bl main
 ldr x1,=exit_block
 str x0,[x1,#8]
 mov x0,#0x20
 hlt #0xf000
 b .
.data
.align 3
exit_block: .quad 0x20026,0
'''
c='''#include "b2l_cpu_affinity.h"
extern int guard_test(uint64_t,uint64_t),identity_test(uint64_t,uint64_t),reset_test(uint64_t,uint64_t);
int main(void){
 const uint64_t ids[]={0x411fd050,0x412fd0a1,0x531f0030,0x000f0030,0x531fd050};
 for(unsigned raw=0;raw<65536;raw++)for(unsigned j=0;j<5;j++)for(unsigned flags=0;flags<4;flags++){
  uint64_t mpidr=raw|(flags==1?0x81000000ULL:flags==2?0x10000ULL:flags==3?(1ULL<<32):0);
  int cpu=b2l_cpu_from_identity(mpidr,ids[j]);
  if(identity_test(mpidr,ids[j])!=cpu)return 1;
  if(guard_test(mpidr,ids[j])!=(cpu>0?1:-1))return 2;
  if(reset_test(mpidr,ids[j])!=(cpu<0?-1:b2l_cpu_firmware_id((unsigned)cpu)))return 3;
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-guard-') as td:
 p=Path(td);(p/'guard.S').write_text(asm);(p/'test.c').write_text(c)
 subprocess.run(['/opt/homebrew/opt/llvm/bin/clang','--target=aarch64-none-elf','--ld-path=/opt/homebrew/bin/ld.lld','-O2','-ffreestanding','-fno-stack-protector','-mgeneral-regs-only','-nostdlib','-I'+str(r/'xnu-overlay/osfmk/arm64'),'-Wl,-Ttext=0x40080000','-Wl,-e,_start',str(p/'guard.S'),str(p/'test.c'),'-o',str(p/'test.elf')],check=True)
 subprocess.run(['qemu-system-aarch64','-machine','virt','-cpu','cortex-a53','-m','128M','-nographic','-semihosting-config','enable=on,target=native','-kernel',str(p/'test.elf')],check=True,timeout=45)
print('Actual identity macro, kernel entry and reset CPU-data key: 1310720 inputs x3 QEMU paths match C PASS')
