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
#include <errno.h>
#define PROTECTION_CLASS_D 4
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
 assert(v.supported&&v.va_dataprotect_class==4);
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
# Compile the actual early setter used before any metadata mutation.
a=s.index('static int hfs_b2l_unencrypted_setclass');b=s.index('\n#endif',a)
helper=s[a:b]
fixture=pre[:pre.index('static void query')]+helper+r'''
int main(void) {
 struct attr v={0,0,3};assert(hfs_b2l_unencrypted_setclass(&v)==0&&!v.supported);
 for(unsigned c=0;c<16;c++) {v=(struct attr){1,0,c};int e=hfs_b2l_unencrypted_setclass(&v);
  assert(e==(c==4?0:ENOTSUP));assert(v.supported==(c==4));assert(v.va_dataprotect_class==c);}
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-hfs-none-') as td:
 p=Path(td);(p/'test.c').write_text(fixture.replace('struct attr','struct vnode_attr'))
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Actual HFS None query/set: class4, reject encrypted classes before mutation; native/non-board modes passed')
