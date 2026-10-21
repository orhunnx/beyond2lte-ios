#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Test actual builtin HFS registration/mount policy and unsealed-root guard."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def function(text, name):
    start = text.index('\n' + name + '(') + 1
    start = text.rfind('\n', 0, start - 1) + 1
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


def main():
    stage = Path(__file__).resolve().parent.parent
    hfs = stage / 'xnu-overlay/bsd/b2l_hfs/b2l_hfs_iokit.c'
    policy = stage / 'xnu-overlay/bsd/kern/b2l_root_policy.c'
    pre = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
typedef int dev_t;
typedef uint64_t user_addr_t;
struct vfsstatfs { char f_fstypename[16]; };
struct mount { uint64_t flags; struct vfsstatfs stat; };
struct vnode { dev_t dev; struct mount *mount; };
typedef struct mount *mount_t;
typedef struct vnode *vnode_t;
typedef void *vfs_context_t;
typedef void *vfstable_t;
struct vfsops { int(*vfs_mount)(mount_t,vnode_t,user_addr_t,vfs_context_t); int preserved; };
struct vnodeopv_desc { int id; };
struct vfs_fsentry { struct vfsops *vfe_vfsops; unsigned vfe_vopcnt; struct vnodeopv_desc **vfe_opvdescs; const char *vfe_fsname; unsigned vfe_flags; };
#define MNT_ROOTFS 1
#define MNT_RDONLY 2
#define DEBUG 0
#define FIFO 1
#undef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 1
#define VFS_TBLNOTYPENUM 1
#define VFS_TBLLOCALVOL 2
#define VFS_TBLREADDIR_EXTENDED 4
#define VFS_TBL64BITREADY 8
#define VFS_TBLVNOP_PAGEOUTV2 16
#define VFS_TBLVNOP_PAGEINV2 32
#define VFS_TBLTHREADSAFE 64
#define VFS_TBLCANMOUNTROOT 128
#define VFS_TBLVNOP_SECLUDE_RENAME 256
#define VFS_TBLNATIVEXATTR 512
#define MAX_CACHED_ORIGINS_DEFAULT 16
#define MAX_CACHED_FILE_ORIGINS_DEFAULT 8
#define kPEICanHasAssertions 1
#define panic(...) abort()
static bool panic_on_assert;
static int rw_flag, unsealed_flag, provider_error, helper_error, underlying_error;
static unsigned sequence, mounted, _hfs_max_origins, _hfs_max_file_origins;
static dev_t selected_dev = 25;
static int b2l_ufs_root_device(dev_t *dev){if(helper_error)return helper_error;*dev=selected_dev;return 0;}
static int PE_parse_boot_argn(const char *key,void *value,size_t n){assert(n==sizeof(int));int v=!strcmp(key,"b2l_root_rw")?rw_flag:unsealed_flag;if(v<0)return 0;*(int*)value=v;return 1;}
static int PE_i_can_has_kernel_configuration(void){return 1;}
static uint64_t vfs_flags(mount_t mp){return mp->flags;}
static int vfs_isrdonly(mount_t mp){return !!(mp->flags&MNT_RDONLY);}
static void vfs_clearflags(mount_t mp,uint64_t flags){mp->flags&=~flags;}
static dev_t vnode_specrdev(vnode_t vp){return vp->dev;}
static mount_t vnode_mount(vnode_t vp){return vp->mount;}
static struct vfsstatfs *vfs_statfs(mount_t mp){return &mp->stat;}
static int original_mount(mount_t mp,vnode_t vp,user_addr_t data,vfs_context_t ctx){(void)mp;(void)vp;(void)data;(void)ctx;mounted++;return underlying_error;}
static const struct vfsops hfs_vfsops={original_mount,0x1234};
static struct vfsops b2l_hfs_vfsops;
static struct vnodeopv_desc hfs_vnodeop_opv_desc={1},hfs_specop_opv_desc={2},hfs_fifoop_opv_desc={3};
static vfstable_t b2l_hfs_vfs_handle;
static int vfs_fsadd(struct vfs_fsentry *entry,vfstable_t *out){assert(sequence++==0);assert(!strcmp(entry->vfe_fsname,"hfs")&&entry->vfe_vopcnt==3);assert(entry->vfe_opvdescs[0]->id==1&&entry->vfe_opvdescs[1]->id==2&&entry->vfe_opvdescs[2]->id==3);assert(entry->vfe_flags==1023);assert(entry->vfe_vfsops->preserved==0x1234&&entry->vfe_vfsops->vfs_mount!=original_mount);*out=(void*)1;return provider_error;}
static void hfs_init_zones(void){assert(sequence++==1);}
static void hfs_sysctl_register(void){assert(sequence++==2);}
static int sysctlbyname(const char*name,void*value,size_t*n,void*in,size_t size){(void)in;(void)size;assert(!strcmp(name,"hw.physicalcpu")&&*n==4);*(uint32_t*)value=16;return 0;}
'''
    body = function(hfs.read_text(), 'b2l_hfs_mount')
    body += function(hfs.read_text(), 'b2l_hfs_register')
    body += function(policy.read_text(), 'b2l_hfs_root_unsealed')
    post = r'''
int main(void){
 b2l_hfs_register();assert(sequence==3&&panic_on_assert&&_hfs_max_origins==32&&_hfs_max_file_origins==32);
 struct mount mp={MNT_ROOTFS|MNT_RDONLY,{"hfs"}};struct vnode vp={25,&mp};
 rw_flag=1;unsealed_flag=1;assert(!b2l_hfs_mount(&mp,&vp,0,NULL)&&!(mp.flags&MNT_RDONLY)&&mounted==1);
 assert(b2l_hfs_root_unsealed(25,&vp));
 for(int v=-1;v<=2;v++){unsealed_flag=v;assert(b2l_hfs_root_unsealed(25,&vp)==(v==1));}
 unsealed_flag=1;assert(!b2l_hfs_root_unsealed(24,&vp)&&!b2l_hfs_root_unsealed(25,NULL));
 strcpy(mp.stat.f_fstypename,"apfs");assert(!b2l_hfs_root_unsealed(25,&vp));strcpy(mp.stat.f_fstypename,"hfs");
 helper_error=ENXIO;assert(!b2l_hfs_root_unsealed(25,&vp));mp.flags=MNT_ROOTFS|MNT_RDONLY;b2l_hfs_mount(&mp,&vp,0,NULL);assert(mp.flags&MNT_RDONLY);helper_error=0;
 for(int v=-1;v<=2;v++){mp.flags=MNT_ROOTFS|MNT_RDONLY;rw_flag=v;b2l_hfs_mount(&mp,&vp,0,NULL);assert(!!(mp.flags&MNT_RDONLY)==(v!=1));}
 rw_flag=1;mp.flags=MNT_ROOTFS|MNT_RDONLY;vp.dev=24;b2l_hfs_mount(&mp,&vp,0,NULL);assert(mp.flags&MNT_RDONLY);vp.dev=25;
 mp.flags=MNT_RDONLY;b2l_hfs_mount(&mp,&vp,0,NULL);assert(mp.flags&MNT_RDONLY);
 mp.flags=MNT_ROOTFS|MNT_RDONLY;underlying_error=EIO;assert(b2l_hfs_mount(&mp,&vp,0,NULL)==EIO);
 puts("HFS registration, mount flags and explicit unsealed-root policy passed");return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='b2l-hfs-provider-test-') as td:
        path = Path(td)
        (path / 'test.c').write_text(pre + body + post)
        compiler = shlex.split(os.environ.get('CC', 'clang'))
        subprocess.run(compiler + ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
        if len(sys.argv) > 1:
            xnu = Path(sys.argv[1]).resolve()
            source = (xnu / 'iokit/bsddev/IOKitBSDInit.cpp').read_text()
            begin = source.index('\tif (!strcmp(rdBootVar, "b2l-system")')
            end = source.index('\n#endif', begin)
            native = source[begin:end]
            cpp = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cerrno>
typedef int dev_t;
#define minor(x) ((x)&0xffffff)
#define major(x) (((x)>>24)&0xff)
#define IOLog(...) ((void)0)
#define panic(...) throw 1
#define OSSafeReleaseNULL(p) do{if(p){releases++;p=nullptr;}}while(0)
static int releases, helper_error;
static dev_t chosen=(9<<24)|25;
static int b2l_ufs_root_device(dev_t*out){if(helper_error)return helper_error;*out=chosen;return 0;}
static void strlcpy(char*out,const char*in,unsigned n){if(n){std::snprintf(out,n,"%s",in);}}
static int select(const char *rdBootVar,char *rootName,unsigned rootNameSize,dev_t*root,unsigned*oflags){
void *matching=(void*)1;
''' + native + r'''
return 0;
iofrootx:return 1;
}
int main(){
 char name[64];dev_t root=-1;unsigned flags=99;
 assert(select("b2l-system",name,sizeof(name),&root,&flags)==1&&root==chosen&&flags==0&&!std::strcmp(name,"b2lufs0p25")&&releases==1);
 assert(select("b2lufs0p25",name,sizeof(name),&root,&flags)==1&&releases==2);
 assert(select("md0",name,sizeof(name),&root,&flags)==0&&releases==2);
 bool failed=false;try{select("b2lufs0p24",name,sizeof(name),&root,&flags);}catch(int){failed=true;}assert(failed);
 failed=false;helper_error=ENXIO;try{select("b2l-system",name,sizeof(name),&root,&flags);}catch(int){failed=true;}assert(failed);
 std::puts("Actual native SYSTEM alias selection passed");
}
'''
            (path / 'native.cpp').write_text(cpp)
            cxx = shlex.split(os.environ.get('CXX', 'clang++'))
            subprocess.run(cxx + ['-xc++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra',
                           '-Werror', '-fsanitize=address,undefined',
                           str(path / 'native.cpp'), '-o', str(path / 'native')], check=True)
            subprocess.run([str(path / 'native')], check=True)


if __name__ == '__main__':
    main()
