#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual identity/learn/persist bodies with concurrent host VFS/I/O.

Host pthreads model the commit mutex; this is not XNU scheduler or hardware proof.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'xnu-overlay/bsd/kern/b2l_native_cache.c').read_text()
def section(start, end):
    a = source.index(start)
    return source[a:source.index(end, a)]
bodies = section('static int identity(', 'static int same_file(')
bodies += section('static int persist(', '\nvoid b2l_native_cache_observe')
worker_struct = section('struct cache_worker {', '\nstatic struct cache_worker workers')
pre = r'''
#include "b2l_nc_store.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sched.h>
#define MAX_DISK (B2L_NC_HEADER+8*B2L_NC_CELL)
#define VREG 1
#define VATTR_INIT(v) memset(v,0,sizeof(*(v)))
#define VATTR_WANTED(v,f) ((void)0)
#define VATTR_IS_SUPPORTED(v,f) 1
struct vnode {uint64_t ino;uint8_t bytes[8192];};
typedef struct vnode *vnode_t;
typedef void *vm_map_t;
struct vnode_attr {uint64_t va_fileid,va_data_size;};
static struct vnode files[4];static vnode_t rootvnode=&files[0];
static int vnode_vtype(vnode_t v){(void)v;return VREG;}
static void *vnode_mount(vnode_t v){(void)v;return &files;}
static void *vfs_context_current(void){return NULL;}
static int vnode_getattr(vnode_t v,struct vnode_attr *a,void *ctx){(void)ctx;a->va_fileid=v->ino;a->va_data_size=sizeof(v->bytes);return 0;}
static void vnode_put(vnode_t v){assert(v);}
static pthread_mutex_t commit_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int held;
static void lck_mtx_lock(pthread_mutex_t *m){assert(!held);assert(!pthread_mutex_lock(m));held=1;}
static void lck_mtx_unlock(pthread_mutex_t *m){assert(held);held=0;assert(!pthread_mutex_unlock(m));}
static pthread_mutex_t barrier_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_cond=PTHREAD_COND_INITIALIZER;
static unsigned arrived,barrier_generation;static int use_barrier;
static void rendezvous(void){
 assert(!pthread_mutex_lock(&barrier_lock));unsigned generation=barrier_generation;
 if(++arrived==4){arrived=0;barrier_generation++;assert(!pthread_cond_broadcast(&barrier_cond));}
 else while(generation==barrier_generation)assert(!pthread_cond_wait(&barrier_cond,&barrier_lock));
 assert(!pthread_mutex_unlock(&barrier_lock));
}
static int rd(vnode_t v,uint64_t off,void *out,unsigned n){
 assert(!held);assert(off<=sizeof(v->bytes)&&n<=sizeof(v->bytes)-off);
 memcpy(out,v->bytes+off,n);if(off==4096&&use_barrier)rendezvous();return 0;
}
struct request {vm_map_t map;uint64_t pc;uint32_t instruction;};
static int b2l_cache_resolve(vm_map_t map,uint64_t pc,vnode_t *vp,uint64_t *off){*vp=map;*off=pc;return 0;}
static uint8_t *snapshot,*disk;
static uint32_t ready;
static uint64_t skipped,dropped,failures,learned;
static struct b2l_nc_key keys[B2L_NC_SLOTS];
static struct b2l_nc_store store;
#define SLOT_COUNT store.slots
static uint32_t buckets[16384],tags[8];
struct integrity_file {int result;struct {uint64_t offset,bytes;} range;};
static struct integrity_file exclusion={1,{4096,4096}};
static struct integrity_file *integrity_file(vnode_t vp,uint64_t size,const uint8_t uuid[16]){
 (void)vp;(void)size;(void)uuid;assert(held);return &exclusion;
}
static int fail_stage,writes;
static int io_write(void *ctx,uint64_t off,const void *in,size_t n){
 (void)ctx;assert(held&&off+n<=MAX_DISK);writes++;if(fail_stage==1)return -1;
 memcpy(disk+off,in,n);return 0;
}
static int io_sync(void *ctx){(void)ctx;assert(held);return fail_stage==2?-1:0;}
static int io_read(void *ctx,uint64_t off,void *out,size_t n){
 (void)ctx;assert(held&&off+n<=MAX_DISK);memcpy(out,disk+off,n);
 if(fail_stage==3)((uint8_t*)out)[100]^=1;return 0;
}
static struct b2l_io cache_io={.read=io_read,.write=io_write,.sync=io_sync,.bytes=B2L_NC_BYTES};
'''
post = r'''
static const uint8_t*slot(unsigned i){const uint8_t*r;assert(!b2l_ncs_read(&store,i,&r));return r;}

static struct cache_worker contexts[4];
struct job {struct request q;struct cache_worker *w;};
static void *run(void *arg){struct job *j=arg;learn(&j->q,j->w);return NULL;}
static void reset(void){
 memset(keys,0,sizeof(keys));memset(snapshot,0,MAX_DISK);memset(disk,0,MAX_DISK);
 memset(&store,0,sizeof(store));memset(buckets,0,sizeof(buckets));memset(tags,0,sizeof(tags));
 store.io=cache_io;store.keys=keys;store.slots=store.used=B2L_NC_SLOTS;
 store.buckets=buckets;store.bucket_count=16384;store.hot=snapshot+B2L_NC_HEADER;store.tags=tags;store.hot_count=8;
 memset(contexts,0,sizeof(contexts));skipped=dropped=failures=learned=0;ready=1;writes=fail_stage=use_barrier=0;
 for(unsigned i=0;i<4;i++){
  memset(&files[i],0,sizeof(files[i]));files[i].ino=42+i;
  b2l_nc_p32(files[i].bytes,0xfeedfacf);b2l_nc_p32(files[i].bytes+4,0x100000c);
  b2l_nc_p32(files[i].bytes+16,1);b2l_nc_p32(files[i].bytes+20,24);
  b2l_nc_p32(files[i].bytes+32,0x1b);b2l_nc_p32(files[i].bytes+36,24);files[i].bytes[40]=(uint8_t)(i+1);
  for(unsigned j=4096;j<8192;j+=4)b2l_nc_p32(files[i].bytes+j,0xd65f0fff);
 }
}
static void batch(unsigned word,int different_files){
 pthread_t ts[4];struct job jobs[4];use_barrier=1;
 for(unsigned i=0;i<4;i++){
  jobs[i]=(struct job){{&files[different_files?i:0],4096+(word+i)*4,0xd65f0fff},&contexts[i]};
  assert(!pthread_create(&ts[i],NULL,run,&jobs[i]));
 }
 for(unsigned i=0;i<4;i++)assert(!pthread_join(ts[i],NULL));use_barrier=0;
}
int main(void){
 snapshot=calloc(1,MAX_DISK);disk=calloc(1,MAX_DISK);assert(snapshot&&disk);reset();
 // All four prepare the same original page before any commit: latest-generation
 // merge must retain every patch, including multiple generations and duplicates.
 for(unsigned i=0;i<16;i++)batch(i*4,0);
 assert(learned==64&&writes==64&&!failures&&!skipped);
 const uint8_t *r=slot(0);assert(r&&b2l_nc_u64(r+8)==64&&keys[0].inode==42&&!keys[1].inode);
 for(unsigned i=0;i<64;i++){
  assert(b2l_nc_u32(r+B2L_NC_DATA+i*4)==0xd65f0fff);
  assert(b2l_nc_u32(r+B2L_NC_DATA+4096+i*4)==0xd65f03c0);
 }
 batch(0,0);assert(learned==64&&writes==64);
 reset();batch(0,1);assert(learned==4&&writes==4);
 for(unsigned i=0;i<4;i++){assert(keys[i].inode>=42&&keys[i].inode<=45);for(unsigned j=0;j<i;j++)assert(keys[i].inode!=keys[j].inode);}
 for(int stage=1;stage<=3;stage++){
  reset();struct request q={&files[0],4096,0xd65f0fff};learn(&q,&contexts[0]);assert(learned==1);
  uint8_t old[B2L_NC_COPY];memcpy(old,slot(0),sizeof(old));fail_stage=stage;batch(4,0);
  assert(!ready&&failures==1&&learned==1&&writes==2);
  assert(!memcmp(slot(0),old,sizeof(old)));assert(!memcmp(disk+B2L_NC_HEADER,old,sizeof(old)));
 }
 reset();for(unsigned i=0;i<B2L_NC_SLOTS;i++)keys[i].inode=1000+i;store.next_free=B2L_NC_SLOTS;
 batch(0,0);assert(dropped==4&&!writes);
 reset();memcpy(files[0].bytes,"dyld_v1",7);memset(files[0].bytes+88,7,16);
 batch(0,0);assert(skipped==4&&!writes); // corecrypto exclusion still enforced
 free(disk);free(snapshot);puts("Actual cache worker bodies: concurrent same-page merge, distinct allocation, duplicate, full cache, exclusion and write/flush/readback failure stop passed");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-workers-',dir='/private/tmp') as tmp:
    c=Path(tmp)/'workers.c';exe=Path(tmp)/'workers'
    c.write_text(pre+worker_struct+'\n'+bodies+post)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread',
                    '-I'+str(root/'common/cache'),str(c),str(root/'common/cache/b2l_cache_core.c'),
                    str(root/'common/cache/CacheSha.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
