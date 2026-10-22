#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the staged HFS attribute block in all board/key-service modes."""
from pathlib import Path
import subprocess,sys,tempfile

source=Path(sys.argv[1])/ 'bsd/b2l_hfs/upstream/core/hfs_vnops.c'
s=source.read_text()
a=s.index('#if CONFIG_PROTECT || defined(ARM64_BOARD_CONFIG_B2L)')
b=s.index('\n\tif (VATTR_IS_ACTIVE(vap, va_write_gencount))',a)
block=s[a:b].rstrip()
assert block.endswith('#endif')
pre=r'''
#include <assert.h>
#include <stdint.h>
struct entry {unsigned cp_pclass;};
struct node {struct entry *c_cpentry;};
struct attr {unsigned active,supported,va_dataprotect_class;};
#define VATTR_IS_ACTIVE(v,a) ((v)->active)
#define VATTR_SET_SUPPORTED(v,a) ((v)->supported=1)
#define CP_CLASS(x) ((x)&31)
static void query(struct node *cp,struct attr *vap){(void)cp;(void)vap;
'''
post=r'''
}
int main(void){
 struct entry e={6};struct node cp={&e};
 struct attr v={0,0,123};query(&cp,&v);assert(!v.supported&&v.va_dataprotect_class==123);
 v.active=1;query(&cp,&v);
#if CONFIG_PROTECT
 assert(v.supported&&v.va_dataprotect_class==6);
 cp.c_cpentry=0;v.va_dataprotect_class=123;query(&cp,&v);assert(v.supported&&v.va_dataprotect_class==0);
#elif defined(ARM64_BOARD_CONFIG_B2L)
 assert(v.supported&&v.va_dataprotect_class==0);
#else
 assert(!v.supported&&v.va_dataprotect_class==123);
#endif
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-hfs-class-') as td:
 p=Path(td);(p/'test.c').write_text(pre+block+post)
 for protected in (0,1):
  for board in (False,True):
   flags=['-DCONFIG_PROTECT='+str(protected)]+(['-DARM64_BOARD_CONFIG_B2L']if board else[])
   subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*flags,str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
print('Actual staged HFS class query: unencrypted 0, native protected values, inactive query and non-board behavior passed')
