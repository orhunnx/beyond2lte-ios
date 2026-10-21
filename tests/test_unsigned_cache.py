#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the actual unsigned cache extent checker and fcntl branch with VFS mocks."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = (Path(sys.argv[1]) / 'bsd/kern/kern_descrip.c').read_text()
helper = source.split('static int\nb2l_unsigned_cache_extent(', 1)[1].split('\n#endif', 1)[0]
helper = 'static int\nb2l_unsigned_cache_extent(' + helper
branch = source.split('\t\tif (cmd == F_ADDFILESIGS_RETURN && !cs_system_enforcement()) {', 1)[1].split('\n#endif', 1)[0]
branch = 'if (cmd == F_ADDFILESIGS_RETURN && !cs_system_enforcement()) {' + branch
harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#define F_ADDFILESIGS_RETURN 97
#define VREG 1
#define os_atomic_inc_orig(p, order) ((*(p))++)
static int enforcement, kind=VREG, size_error, copy_error, puts_count, copies;
static off_t mock_size=638976, returned;
static int cs_system_enforcement(void) { return enforcement; }
static int vnode_vtype(int *p) { (void)p; return kind; }
static int vnode_size(int *p, off_t *s, int *c) {
 (void)p; (void)c; *s=mock_size; return size_error;
}
static int copyout(const void *p, uintptr_t a, size_t n) {
 (void)a; ++copies; if (!copy_error) memcpy(&returned,p,n); return copy_error;
}
static void vnode_put(int *p) { (void)p; ++puts_count; }
""" + helper + r"""
static int registration(int cmd) {
 int vnode=0, *vp=&vnode, context=0, error=0;
 uintptr_t argp=0;
 struct { off_t fs_file_start; uint64_t fs_blob_start, fs_blob_size; }
 fs = {0,622592,16384};
 switch(cmd) { case F_ADDFILESIGS_RETURN: case 1:
""" + branch + r"""
 return -1; /* original enforcement path */
 }
 outdrop: return error;
}
int main(void) {
 off_t extent=123;
 assert(b2l_unsigned_cache_extent(638976,0,622592,16384,&extent)==0 && extent==638976);
 assert(b2l_unsigned_cache_extent(100,10,70,20,&extent)==0 && extent==90);
 #define REJECT(sz,base,off,len,err) do { extent=123; \
 assert(b2l_unsigned_cache_extent(sz,base,off,len,&extent)==err && extent==123); } while(0)
 REJECT(-1,0,0,1,EINVAL); REJECT(10,-1,0,1,EINVAL);
 REJECT(10,11,0,1,EINVAL); REJECT(10,0,0,0,EINVAL);
 REJECT(10,0,UINT64_MAX,1,EINVAL); REJECT(INT64_MAX,0,0,UINT64_MAX,E2BIG);
 REJECT(10,0,9,2,EINVAL); REJECT(10,10,0,1,EINVAL);
 enforcement=1; assert(registration(97)==-1 && !puts_count && !copies);
 enforcement=0; assert(registration(1)==-1 && !puts_count && !copies);
 assert(registration(97)==0 && returned==638976 && puts_count==1 && copies==1);
 copy_error=EFAULT; assert(registration(97)==EFAULT && puts_count==2 && copies==2);
 copy_error=0; size_error=EIO; assert(registration(97)==EIO && puts_count==3 && copies==2);
 size_error=0; kind=0; assert(registration(97)==EINVAL && puts_count==4 && copies==2);
 kind=VREG; mock_size=1; assert(registration(97)==EINVAL && puts_count==5 && copies==2);
 puts("Unsigned cache: bounds, overflow, policy gates, VFS and copyout failures passed");
}
"""
with tempfile.TemporaryDirectory(prefix='b2l-cache-test-') as td:
    c=Path(td)/'test.c'; exe=Path(td)/'test'
    c.write_text(harness)
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    subprocess.run(['xcrun','clang','-isysroot',sdk,'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
