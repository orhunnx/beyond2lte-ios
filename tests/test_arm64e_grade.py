#!/usr/bin/env python3
"""Compile actual grading/process-classification functions with public ABI fixtures."""
from pathlib import Path
import re,sys,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=Path(sys.argv[1]) if len(sys.argv)>1 else root/'modified'
s=(source/'bsd/dev/arm/kern_machdep.c').read_text()
def function(name):
 match=re.search(r'\n(?:static\s+)?(?:int|bool)\n'+name+r'\([^;]*?\)\n\{',s,re.S)
 if not match:raise RuntimeError('missing actual function '+name)
 start=match.start(); p=s.index('{',match.start());depth=1;q=p+1
 while depth:
  if s[q]=='{':depth+=1
  elif s[q]=='}':depth-=1
  q+=1
 return s[start:q]
header=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define __arm64__ 1
#define XNU_TARGET_OS_IOS 1
#define XNU_TARGET_OS_XR 0
#define ARM64_BOARD_CONFIG_B2L 1
#define __unused __attribute__((unused))
typedef int cpu_type_t;
typedef int cpu_subtype_t;
#define CPU_ARCH_ABI64 0x1000000
#define CPU_TYPE_ARM64 (CPU_ARCH_ABI64|12)
#define CPU_SUBTYPE_ARM64_ALL 0
#define CPU_SUBTYPE_ARM64_V8 1
#define CPU_SUBTYPE_ARM64E 2
#define CPU_SUBTYPE_MASK ((int)0xff000000)
#define CPU_SUBTYPE_PTRAUTH_ABI ((int)0x80000000)
#define CPU_SUBTYPE_ARM64_PTR_AUTH_MAX_PREFERRED_VERSION 0
#define CPU_SUBTYPE_ARM64_PTR_AUTH_VERSION(v) (((v)&0x0f000000)>>24)
static bool enabled;
static int host=CPU_SUBTYPE_ARM64_V8;
static int cpu_subtype(void){return host;}
static int cpu_subtype32(void){return 0;}
static bool b2l_arm64e_compat_enabled(void){return enabled;}
struct proc {int p_cputype,p_cpusubtype;};
typedef struct proc*proc_t;
static struct proc process;
static proc_t current;
static proc_t current_proc(void){return current;}
'''
# Host SDK may already define __unused.
header=header.replace('#define __unused __attribute__((unused))','#ifndef __unused\n#define __unused __attribute__((unused))\n#endif')
body='\n'.join(function(n) for n in ['grade_arm64e_binary','ml_grade_binary','b2l_arm64e_current_task'])
main=r'''
int main(void){
assert(!ml_grade_binary(CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64E,CPU_SUBTYPE_PTRAUTH_ABI,true));
enabled=true;
assert(ml_grade_binary(CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64E,CPU_SUBTYPE_PTRAUTH_ABI,true)==12);
assert(!ml_grade_binary(CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64E,0,true));
assert(!ml_grade_binary(CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64E,CPU_SUBTYPE_PTRAUTH_ABI|0x01000000,true));
assert(ml_grade_binary(CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64_ALL,0,true)==9);
assert(ml_grade_binary(CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64_V8,0,true)==10);
assert(!ml_grade_binary(7,CPU_SUBTYPE_ARM64E,CPU_SUBTYPE_PTRAUTH_ABI,true));
assert(host==CPU_SUBTYPE_ARM64_V8);
assert(!b2l_arm64e_current_task());current=&process;
process.p_cputype=CPU_TYPE_ARM64;process.p_cpusubtype=CPU_SUBTYPE_ARM64E|CPU_SUBTYPE_PTRAUTH_ABI;
assert(b2l_arm64e_current_task());process.p_cputype=7;assert(!b2l_arm64e_current_task());
process.p_cputype=CPU_TYPE_ARM64;process.p_cpusubtype=CPU_SUBTYPE_ARM64_V8;assert(!b2l_arm64e_current_task());
puts("Actual-source ARM64e grade/process opt-in gate PASS");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='b2l-grade-') as d:
 d=Path(d);(d/'test.c').write_text(header+body+main)
 cc=subprocess.check_output(['xcrun','--find','clang'],text=True).strip()
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run([cc,'-isysroot',sdk,'-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
