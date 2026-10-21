#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual bounded byte/GPT code plus BSD strategy/ioctl bodies, ASan/UBSan.

All disk fixtures are synthetic and generated in the test. No device sectors,
GUIDs, serials or private headers are included.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def function(text, name):
    start = text.index("\n" + name + "(") + 1
    # Include the immediately preceding return type, e.g. static int.
    start = text.rfind("\n", 0, start - 1) + 1
    if text[start:].startswith("b2l_"):
        raise AssertionError(name)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def main():
    xnu = Path(sys.argv[1]).resolve()
    source = (xnu / "bsd/dev/b2l_ufs_disk.c").read_text()
    pre = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include "b2l_ufs_core.h"
#include "b2l_ufs_transport.h"
#ifndef FWRITE
#define FWRITE 2
#endif
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#undef minor
#undef makedev
#define minor(x) ((unsigned)(x) & 0xffffU)
#define makedev(m,n) ((dev_t)(((unsigned)(m)<<16)|(n)))
#define B_READ 1
#define DKIOCGETBLOCKSIZE 1
#define DKIOCGETPHYSICALBLOCKSIZE 2
#define DKIOCGETBLOCKCOUNT 3
#define DKIOCSETBLOCKSIZE 4
#define DKIOCISWRITABLE 5
#define DKIOCISSOLIDSTATE 6
#define DKIOCGETBASE 7
#define DKIOCGETFEATURES 8
#define DKIOCGETMAXBLOCKCOUNTREAD 9
#define DKIOCGETMAXBYTECOUNTREAD 10
#define DKIOCGETMAXSEGMENTCOUNTREAD 11
#define DKIOCGETMAXBLOCKCOUNTWRITE 12
#define DKIOCGETMAXBYTECOUNTWRITE 13
#define DKIOCGETMAXSEGMENTCOUNTWRITE 14
#define DKIOCSYNCHRONIZE 15
#define DKIOCSYNCHRONIZECACHE 16
#define UIO_READ 0
#define UIO_WRITE 1
struct proc;
struct uio { int64_t offset, resid; int rw; unsigned char *data; size_t cursor; size_t iov_len[8]; unsigned iov_index,iov_count; };
struct buf { dev_t dev; uint32_t count,resid; int flags,error,completed,mapped; int64_t blkno; unsigned char *data; };
static unsigned held, read_calls, write_calls, sync_calls, map_calls, fault_call, fault_write_call;
static int map_error, sync_error, move_error, move_calls, fault_move_call, b2l_ufs_disk_lock;
static unsigned char image[256*4096], scratch[4096];
static uint32_t bs=4096;
static struct b2l_ufs_reader b2l_reader;
static struct b2l_gpt b2l_partitions;
static unsigned b2l_system_index;
static int b2l_block_major = 42;
static char last_log[512];
static int host_printf(const char *fmt,...){va_list ap;va_start(ap,fmt);int n=vsnprintf(last_log,sizeof(last_log),fmt,ap);va_end(ap);return n;}
#define printf host_printf
struct b2l_ufs_view { uint64_t base,size; uint32_t sector_size; int ready,writable; void *block_node,*char_node; };
static struct b2l_ufs_view b2l_views[B2L_GPT_MAX_ENTRIES+1];
static void lck_mtx_lock(int *l){(void)l; assert(!held);held=1;}
static void lck_mtx_unlock(int *l){(void)l;assert(held);held=0;}
static dev_t buf_device(struct buf*b){return b->dev;}
static uint32_t buf_count(struct buf*b){return b->count;}
static void buf_setresid(struct buf*b,uint32_t n){b->resid=n;}
static int buf_flags(struct buf*b){return b->flags;}
static int64_t buf_blkno(struct buf*b){return b->blkno;}
static void buf_seterror(struct buf*b,int e){b->error=e;}
static void buf_biodone(struct buf*b){assert(!b->completed);b->completed=1;}
static int buf_map(struct buf*b,caddr_t*out){map_calls++;if(map_error)return map_error;b->mapped=1;*out=(void*)b->data;return 0;}
static void buf_unmap(struct buf*b){assert(b->mapped);b->mapped=0;}
static int uio_rw(struct uio*u){return u->rw;}
static int64_t uio_resid(struct uio*u){return u->resid;}
static int64_t uio_offset(struct uio*u){return u->offset;}
static size_t uio_curriovlen(struct uio*u){return u->iov_index<u->iov_count?u->iov_len[u->iov_index]:0;}
static struct uio*uio_duplicate(struct uio*u){struct uio*p=malloc(sizeof(*p));if(p)*p=*u;return p;}
static void uio_free(struct uio*u){free(u);}
/* Faithful XNU contract: update ONLY current iovec but subtract full n/resid. */
static void uio_update(struct uio*u,size_t n){
 if(u->iov_index>=u->iov_count)return;assert(n<=(uint64_t)u->resid);
 size_t take=n>u->iov_len[u->iov_index]?u->iov_len[u->iov_index]:n;
 u->iov_len[u->iov_index]-=take;u->cursor+=take;u->offset+=n;u->resid-=n;
 while(u->iov_index<u->iov_count&&!u->iov_len[u->iov_index])u->iov_index++;
}
static int uiomove(caddr_t data,int count,struct uio*u){
 assert(!held);move_calls++;if(move_error)return move_error;
 assert(count>=0&&count<=u->resid);size_t remaining=(size_t)count;
 while(remaining){uio_update(u,0);size_t n=uio_curriovlen(u);assert(n);if(n>remaining)n=remaining;
  if(fault_move_call&&move_calls==fault_move_call){if(n>1)n/=2;
   if(u->rw==UIO_WRITE)memcpy(data,u->data+u->cursor,n);else memcpy(u->data+u->cursor,data,n);
   uio_update(u,n);return EFAULT;}
  if(u->rw==UIO_WRITE)memcpy(data,u->data+u->cursor,n);else memcpy(u->data+u->cursor,data,n);
  uio_update(u,n);data+=n;remaining-=n;
 }return 0;
}
static struct uio make_uio(int64_t offset,int64_t count,unsigned char*data){
 struct uio u={.offset=offset,.resid=count,.rw=UIO_WRITE,.data=data,.iov_len={(size_t)count},.iov_count=1};return u;
}
static int media_read(uint64_t lba,uint32_t count,void*out){
 read_calls++;if(fault_call&&read_calls==fault_call)return EIO;
 assert(count==1&&lba<256);memcpy(out,image+lba*bs,bs);return 0;
}
static int media_write(uint64_t lba,uint32_t count,const void*in){
 write_calls++;if(fault_write_call&&write_calls==fault_write_call)return EIO;
 assert(count&&count*bs<=65536&&lba<256&&count<=256-lba);
 assert(lba*bs>=b2l_reader.writable_base&&(lba+count)*bs<=b2l_reader.writable_base+b2l_reader.writable_size);
 memcpy(image+lba*bs,in,count*bs);return 0;
}
int b2l_ufs_transport_sync(void){assert(held);sync_calls++;return sync_error;}
'''
    bodies = "\n".join(function(source, name) for name in
                         ("b2l_view", "b2l_open", "b2l_strategy", "b2l_write", "b2l_ioctl_common", "b2l_probe_selected", "b2l_ufs_root_device"))
    post = r'''
static void w32(unsigned char*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));}
static void w64(unsigned char*p,uint64_t n){w32(p,(uint32_t)n);w32(p+4,(uint32_t)(n>>32));}
static uint32_t crc(const unsigned char*p,size_t n){uint32_t c=UINT32_MAX;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320U&(0U-(c&1)));}return c^UINT32_MAX;}
static uint64_t backup_table(void){return 255-(8*128+bs-1)/bs;}
static unsigned char*entry(unsigned index){return image+2*bs+index*128;}
static void fix_header(unsigned char*h){w32(h+16,0);w32(h+16,crc(h,92));}
static void retable(void){
 uint32_t c=crc(image+2*bs,8*128);memcpy(image+backup_table()*bs,image+2*bs,8*128);
 w32(image+bs+88,c);w32(image+255*bs+88,c);
 fix_header(image+bs);fix_header(image+255*bs);
}
static void fixture(uint32_t block_size){
 bs=block_size;read_calls=write_calls=sync_calls=fault_call=fault_write_call=map_calls=map_error=sync_error=move_error=move_calls=fault_move_call=held=0;memset(image,0,sizeof(image));
 b2l_reader=(struct b2l_ufs_reader){.block_count=256,.block_size=bs,.read=media_read,.scratch=scratch,.write=media_write};
 b2l_system_index=0;
 for(unsigned k=0;k<2;k++){
  unsigned char*h=image+(k?255:1)*bs;memcpy(h,"EFI PART",8);w32(h+8,0x10000);w32(h+12,92);
  w64(h+24,k?255:1);w64(h+32,k?1:255);w64(h+40,8);w64(h+48,252);
  /* Deliberately synthetic GUID, never derived from a device. */
  h[56]=0x11;w64(h+72,k?backup_table():2);w32(h+80,8);w32(h+84,128);
 }
 for(unsigned k=0;k<2;k++){
  unsigned char*p=entry(k);p[0]=0x22;p[16]=(unsigned char)(k+1);
  w64(p+32,k?32:16);w64(p+40,k?47:23);
  const char*name=k?"super":"SYSTEM";for(unsigned i=0;name[i];i++)p[56+2*i]=(unsigned char)name[i];
 }
 retable();
}
static void gpt_tests(void){
 struct b2l_gpt*g=malloc(sizeof(*g));assert(g);
 for(unsigned n=0;n<2;n++){
  fixture(n?4096:512);assert(!b2l_gpt_read(&b2l_reader,g));
  assert(g->entry_count==8&&g->partition_count==2&&g->partitions[0].first_lba==16&&g->partitions[0].block_count==8);
  assert(b2l_gpt_label_equal(g->partitions[0].name,"system"));assert(!b2l_gpt_label_equal(g->partitions[0].name,"system_a"));
  assert(g->partitions[1].index==2&&!strcmp(g->partitions[1].name,"super"));
 }
 fixture(4096);image[bs+16]^=1;assert(b2l_gpt_read(&b2l_reader,g)==EINVAL&&g->partition_count==0);
 fixture(4096);image[255*bs+16]^=1;assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);image[2*bs+100]^=1;assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);image[backup_table()*bs+100]^=1;assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w64(image+bs+72,8);fix_header(image+bs);assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w64(image+255*bs+72,252);fix_header(image+255*bs);assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w64(image+bs+24,2);fix_header(image+bs);assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w64(image+bs+32,254);fix_header(image+bs);assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w32(image+bs+80,UINT32_MAX);fix_header(image+bs);assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w32(image+bs+84,384);fix_header(image+bs);assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w64(entry(0)+32,253);w64(entry(0)+40,254);retable();assert(b2l_gpt_read(&b2l_reader,g)==EINVAL&&g->partition_count==0);
 fixture(4096);w64(entry(0)+40,15);retable();assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);w64(entry(1)+32,20);retable();assert(b2l_gpt_read(&b2l_reader,g)==EINVAL&&g->partition_count==0);
 fixture(4096);memset(entry(0)+16,0,16);retable();assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);entry(0)[57]=0xd8;retable();assert(!b2l_gpt_read(&b2l_reader,g)&&g->partitions[0].name[0]=='?');
 fixture(4096);fault_call=3;assert(b2l_gpt_read(&b2l_reader,g)==EIO&&g->partition_count==0);
 fixture(4096);b2l_reader.block_count=UINT64_MAX;assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 fixture(4096);b2l_reader.block_size=768;assert(b2l_gpt_read(&b2l_reader,g)==EINVAL);
 free(g);
}
static void read_tests(void){
 fixture(4096);for(size_t i=0;i<sizeof(image);i++)image[i]=(unsigned char)(i*17+i/4096);
 unsigned char out[8200];size_t done;memset(out,0xa5,sizeof(out));
 assert(!b2l_ufs_read_bytes(&b2l_reader,4095,8192,out+1,&done)&&done==8192);
 assert(!memcmp(out+1,image+4095,8192)&&out[0]==0xa5&&out[8193]==0xa5);
 unsigned before=read_calls;assert(b2l_ufs_read_view(&b2l_reader,UINT64_MAX,UINT64_MAX,UINT64_MAX,SIZE_MAX,1,NULL,&done)==EROFS&&done==0&&read_calls==before);
 assert(b2l_ufs_read_view(&b2l_reader,UINT64_MAX,1,0,1,0,out,&done)==EINVAL);
 assert(b2l_ufs_read_view(&b2l_reader,4096,4096,4097,1,0,out,&done)==EINVAL);
 assert(!b2l_ufs_read_view(&b2l_reader,4096,4096,4096,1,0,out,&done)&&done==0);
 assert(!b2l_ufs_read_view(&b2l_reader,4096,4096,4090,100,0,out,&done)&&done==6&&!memcmp(out,image+8186,6));
 before=read_calls;fault_call=before+2;assert(b2l_ufs_read_view(&b2l_reader,0,sizeof(image),0,8192,0,out,&done)==EIO&&done==4096);
 fault_call=0;assert(b2l_ufs_read_bytes(&b2l_reader,0,1,NULL,&done)==EINVAL);
}
static void write_view_tests(void){
 fixture(4096);for(size_t i=0;i<sizeof(image);i++)image[i]=(unsigned char)(i*23+i/4096);
 unsigned char *before=malloc(sizeof(image));assert(before);memcpy(before,image,sizeof(image));
 unsigned char data[131072];for(size_t i=0;i<sizeof(data);i++)data[i]=(unsigned char)(i*11+19);
 size_t done=99;
 assert(b2l_ufs_write_view(&b2l_reader,4096,131072,0,512,data,&done)==EROFS&&done==0&&!write_calls);
 b2l_reader.writable_base=4096;b2l_reader.writable_size=131072;
 /* Whole-LU aliases and any neighboring partition remain read-only. */
 assert(b2l_ufs_write_view(&b2l_reader,0,sizeof(image),4096,512,data,&done)==EROFS&&!write_calls&&!read_calls);
 assert(b2l_ufs_write_view(&b2l_reader,8192,4096,0,512,data,&done)==EROFS&&!write_calls);
 /* Request crosses the end: fail BEFORE writing even the in-range prefix. */
 assert(b2l_ufs_write_view(&b2l_reader,4096,131072,131071,2,data,&done)==EINVAL&&done==0&&!write_calls&&!read_calls);
 assert(b2l_ufs_write_view(&b2l_reader,4096,131072,UINT64_MAX,SIZE_MAX,data,&done)==EINVAL&&!write_calls);
 assert(b2l_ufs_write_view(&b2l_reader,4096,131072,0,1,NULL,&done)==EINVAL&&!write_calls);
 /* Cross two 4KiB boundaries; preserve every byte outside requested payload. */
 assert(!b2l_ufs_write_view(&b2l_reader,4096,131072,4095,8192,data,&done)&&done==8192);
 assert(write_calls==3&&read_calls==2&&!memcmp(image+8191,data,8192));
 assert(!memcmp(image,before,8191)&&!memcmp(image+16383,before+16383,sizeof(image)-16383));
 /* Native aligned multi-block writes batch at the transport's 64KiB ceiling. */
 read_calls=write_calls=0;assert(!b2l_ufs_write_view(&b2l_reader,4096,131072,0,sizeof(data),data,&done));
 assert(done==sizeof(data)&&write_calls==2&&!read_calls);
 unsigned prev=write_calls;fault_call=read_calls+1;
 assert(b2l_ufs_write_view(&b2l_reader,4096,131072,1,1,data,&done)==EIO&&done==0&&write_calls==prev);
 fault_call=0;read_calls=write_calls=0;fault_write_call=2;
 assert(b2l_ufs_write_view(&b2l_reader,4096,131072,4095,8192,data,&done)==EIO&&done==1&&write_calls==2);
 fault_write_call=0;assert(!b2l_ufs_write_view(&b2l_reader,4096,131072,131072,0,NULL,&done)&&done==0);
 free(before);
}
static void strategy_tests(void){
 fixture(4096);for(size_t i=0;i<sizeof(image);i++)image[i]=(unsigned char)(i*7+i/512);
 memset(b2l_views,0,sizeof(b2l_views));b2l_views[1]=(struct b2l_ufs_view){.base=4096,.size=8192,.sector_size=512,.ready=1};
 unsigned char out[10000];memset(out,0xa5,sizeof(out));
 struct buf b={.dev=1,.count=1024,.flags=B_READ,.blkno=1,.data=out+1};b2l_strategy(&b);
 assert(b.completed&&!b.error&&!b.resid&&!b.mapped&&!held&&!memcmp(out+1,image+4608,1024)&&out[0]==0xa5&&out[1025]==0xa5);
 unsigned before=read_calls,maps=map_calls;b=(struct buf){.dev=1,.count=512,.flags=0,.data=out};b2l_strategy(&b);
 assert(b.completed&&b.error==EROFS&&b.resid==512&&read_calls==before&&map_calls==maps);
 b=(struct buf){.dev=1,.count=512,.flags=B_READ,.blkno=-1,.data=out};b2l_strategy(&b);assert(b.error==EINVAL&&read_calls==before);
 b=(struct buf){.dev=1,.count=512,.flags=B_READ,.blkno=INT64_MAX,.data=out};b2l_strategy(&b);assert(b.error==EINVAL&&read_calls==before);
 b=(struct buf){.dev=999,.count=512,.flags=B_READ,.data=out};b2l_strategy(&b);assert(b.error==ENXIO&&b.resid==512);
 b=(struct buf){.dev=1,.count=1024,.flags=B_READ,.blkno=15,.data=out};b2l_strategy(&b);assert(!b.error&&b.resid==512);
 b=(struct buf){.dev=1,.count=512,.flags=B_READ,.blkno=16,.data=out};b2l_strategy(&b);assert(!b.error&&b.resid==512);
 b=(struct buf){.dev=1,.count=512,.flags=B_READ,.blkno=17,.data=out};b2l_strategy(&b);assert(b.error==EINVAL&&b.resid==512);
 map_error=ENOMEM;b=(struct buf){.dev=1,.count=512,.flags=B_READ,.data=out};b2l_strategy(&b);assert(b.error==ENOMEM&&!b.mapped&&!held);map_error=0;
 fault_call=read_calls+2;b=(struct buf){.dev=1,.count=8192,.flags=B_READ,.data=out};b2l_strategy(&b);assert(b.error==EIO&&b.resid==4096&&!b.mapped&&!held);fault_call=0;
 assert(b2l_open(1,FWRITE,0,NULL)==EROFS&&b2l_open(999,0,0,NULL)==ENXIO&&!b2l_open(1,0,0,NULL));
 assert(b2l_write(1,NULL,0)==EROFS);
 uint64_t value=99;assert(!b2l_ioctl_common(1,DKIOCISWRITABLE,(void*)&value,0)&&value==0);
 assert(!b2l_ioctl_common(1,DKIOCGETPHYSICALBLOCKSIZE,(void*)&value,0)&&value==4096);
 value=0;assert(!b2l_ioctl_common(1,DKIOCGETBLOCKCOUNT,(void*)&value,0)&&value==16);
 value=4096;assert(!b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,0));
 value=0;assert(!b2l_ioctl_common(1,DKIOCGETBLOCKCOUNT,(void*)&value,0)&&value==2);
 value=1024;assert(b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,0)==EINVAL);
 value=512;assert(b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,1)==ENODEV);
 assert(!b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,0));
 assert(b2l_ioctl_common(1,999,(void*)&value,0)==ENOTTY&&!held);
}
static void write_adapter_tests(void){
 fixture(4096);memset(b2l_views,0,sizeof(b2l_views));
 b2l_views[0]=(struct b2l_ufs_view){.base=0,.size=sizeof(image),.sector_size=512,.ready=1};
 b2l_views[1]=(struct b2l_ufs_view){.base=4096,.size=8192,.sector_size=512,.ready=1,.writable=1};
 b2l_reader.writable_base=4096;b2l_reader.writable_size=8192;
 unsigned char in[9000];for(size_t i=0;i<sizeof(in);i++)in[i]=(unsigned char)(i*13);
 struct buf b={.dev=1,.count=1024,.flags=0,.blkno=1,.data=in};b2l_strategy(&b);
 assert(b.completed&&!b.error&&!b.resid&&!held&&!memcmp(image+4608,in,1024));
 unsigned before=write_calls,reads=read_calls;
 b=(struct buf){.dev=0,.count=512,.flags=0,.blkno=8,.data=in};b2l_strategy(&b);
 assert(b.completed&&b.error==EROFS&&write_calls==before&&read_calls==reads);
 b=(struct buf){.dev=1,.count=1024,.flags=0,.blkno=15,.data=in};b2l_strategy(&b);
 assert(b.completed&&b.error==EINVAL&&b.resid==1024&&write_calls==before&&read_calls==reads);
 assert(!b2l_open(1,FWRITE,0,NULL)&&b2l_open(0,FWRITE,0,NULL)==EROFS);
 uint64_t value=0;assert(!b2l_ioctl_common(1,DKIOCISWRITABLE,(void*)&value,0)&&value==1);
 assert(!b2l_ioctl_common(1,DKIOCGETMAXBYTECOUNTWRITE,(void*)&value,0)&&value==65536);
 assert(!b2l_ioctl_common(1,DKIOCGETMAXBLOCKCOUNTWRITE,(void*)&value,0)&&value==128);
 assert(!b2l_ioctl_common(1,DKIOCGETMAXSEGMENTCOUNTWRITE,(void*)&value,0)&&value==1);
 assert(!b2l_ioctl_common(1,DKIOCSYNCHRONIZECACHE,NULL,0)&&sync_calls==1);
 sync_error=EIO;assert(b2l_ioctl_common(1,DKIOCSYNCHRONIZE,NULL,0)==EIO&&sync_calls==2);sync_error=0;
 assert(b2l_ioctl_common(0,DKIOCSYNCHRONIZECACHE,NULL,0)==EROFS&&sync_calls==2);
 /* Character copyin is unlocked; original UIO advances only for completed writes. */
 struct uio u=make_uio(4095,4097,in);
 assert(!b2l_write(1,&u,0)&&u.resid==0&&u.offset==8192&&u.cursor==4097&&!held);
 assert(!memcmp(image+8191,in,4097));
 before=write_calls;u=make_uio(8191,2,in);
 assert(b2l_write(1,&u,0)==EINVAL&&u.resid==2&&u.offset==8191&&write_calls==before);
 move_error=EFAULT;u=make_uio(0,512,in);
 assert(b2l_write(1,&u,0)==EFAULT&&u.resid==512&&write_calls==before);move_error=0;
 fault_write_call=write_calls+1;u=make_uio(0,512,in);
 assert(b2l_write(1,&u,0)==EIO&&u.resid==512&&u.offset==0);fault_write_call=0;
 /* Leading/embedded zero iovecs and fragments smaller than the 4KiB bounce. */
 u=(struct uio){.offset=0,.resid=4097,.rw=UIO_WRITE,.data=in,.iov_len={0,1,0,1000,3096},.iov_count=5};
 assert(!b2l_write(1,&u,0)&&!u.resid&&u.offset==4097&&u.cursor==4097&&u.iov_index==5);
 assert(!memcmp(image+4096,in,4097));
 /* Fault halfway through the second iovec: only first iovec reaches media. */
 u=(struct uio){.offset=0,.resid=1024,.rw=UIO_WRITE,.data=in,.iov_len={512,512},.iov_count=2};
 move_calls=0;fault_move_call=2;before=write_calls;
 assert(b2l_write(1,&u,0)==EFAULT&&u.resid==512&&u.offset==512&&u.cursor==512&&u.iov_index==1);
 assert(write_calls==before+1&&u.iov_len[1]==512);fault_move_call=0;
}
static void be32(unsigned char*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(24-8*i));}
static void hfs_header(unsigned lba,unsigned version){
 unsigned char *h=image+lba*bs+1024;h[0]='H';h[1]=version==5?'X':'+';h[2]=0;h[3]=(unsigned char)version;
 be32(h+40,512);be32(h+44,8*bs/512);be32(h+48,0);
}
static void signature_tests(void){
 fixture(4096);assert(!b2l_gpt_read(&b2l_reader,&b2l_partitions));
 /* A partition named SYSTEM can really be the Android dynamic container. */
 unsigned char *magic=image+16*bs+4096;magic[0]=0x67;magic[1]=0x44;magic[2]=0x6c;magic[3]=0x61;
 b2l_probe_selected();assert(strstr(last_log,"SYSTEM p1")&&strstr(last_log,"container signature=Android LP geometry"));
 memset(magic,0,4);image[16*bs+1024]='H';image[16*bs+1025]='+';
 b2l_probe_selected();assert(strstr(last_log,"partition signature=HFS+/HFSX"));
 image[16*bs+1024]=0;image[16*bs+1025]=0;image[16*bs+1080]=0x53;image[16*bs+1081]=0xef;
 b2l_probe_selected();assert(strstr(last_log,"signature=ext4-family"));
 strcpy(b2l_partitions.partitions[1].name,"system");unsigned before=read_calls;
 b2l_probe_selected();assert(strstr(last_log,"matches=2")&&read_calls==before);
 assert(!b2l_system_index&&!b2l_reader.writable_size);
 fixture(4096);assert(!b2l_gpt_read(&b2l_reader,&b2l_partitions));hfs_header(16,4);
 b2l_probe_selected();assert(b2l_system_index==1&&b2l_reader.writable_base==16*bs&&b2l_reader.writable_size==8*bs);
 /* SYSTEM is selected, not a separate super or the containing whole-LU alias. */
 assert(strstr(last_log,"SYSTEM read-write authorized"));
 memset(b2l_views,0,sizeof(b2l_views));b2l_views[1]=(struct b2l_ufs_view){.ready=1,.writable=1};
 dev_t dev=0;assert(!b2l_ufs_root_device(&dev)&&dev==makedev(42,1));
 b2l_views[1].writable=0;assert(b2l_ufs_root_device(&dev)==ENXIO);assert(b2l_ufs_root_device(NULL)==EINVAL);
 /* Invalid geometry and wrong HFS version do not grant any writes. */
 be32(image+16*bs+1024+44,UINT32_MAX);b2l_probe_selected();assert(!b2l_system_index&&!b2l_reader.writable_size);
 hfs_header(16,4);image[16*bs+1024+3]=5;b2l_probe_selected();assert(!b2l_system_index);
 /* A uniquely named super is allowed only as the absence-of-SYSTEM fallback. */
 fixture(4096);assert(!b2l_gpt_read(&b2l_reader,&b2l_partitions));strcpy(b2l_partitions.partitions[0].name,"VENDOR");hfs_header(32,5);
 b2l_probe_selected();assert(b2l_system_index==2&&b2l_reader.writable_base==32*bs);
 strcpy(b2l_partitions.partitions[0].name,"super");before=read_calls;b2l_probe_selected();
 assert(!b2l_system_index&&!b2l_reader.writable_size&&read_calls==before);
}
int main(void){gpt_tests();read_tests();write_view_tests();strategy_tests();write_adapter_tests();signature_tests();puts("UFS GPT/SYSTEM write/RMW/BSD strategy/flush/root tests passed");return 0;}
'''
    with tempfile.TemporaryDirectory(prefix="b2l-ufs-test-") as td:
        path = Path(td)
        cfile = path / "test.c"
        cfile.write_text(pre + bodies + post)
        binary = path / "test"
        compiler = shlex.split(os.environ.get("CC", "clang"))
        subprocess.run(compiler + ["-std=c11", "-D_DARWIN_C_SOURCE", "-DB2L_UFS_HOST_TEST",
                       "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-g", "-O1",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                       "-I" + str(xnu / "bsd/dev"), str(cfile),
                       str(xnu / "bsd/dev/b2l_ufs_core.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
