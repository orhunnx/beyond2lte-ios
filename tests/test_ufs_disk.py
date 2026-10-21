#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual read-only byte/GPT code plus BSD strategy/ioctl bodies, ASan/UBSan.

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
#define minor(x) ((unsigned)(x))
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
struct proc;
struct uio;
struct buf { dev_t dev; uint32_t count,resid; int flags,error,completed,mapped; int64_t blkno; unsigned char *data; };
static unsigned held, read_calls, map_calls, fault_call;
static int map_error, b2l_ufs_disk_lock;
static unsigned char image[256*4096], scratch[4096];
static uint32_t bs=4096;
static struct b2l_ufs_reader b2l_reader;
static struct b2l_gpt b2l_partitions;
static char last_log[512];
static int host_printf(const char *fmt,...){va_list ap;va_start(ap,fmt);int n=vsnprintf(last_log,sizeof(last_log),fmt,ap);va_end(ap);return n;}
#define printf host_printf
struct b2l_ufs_view { uint64_t base,size; uint32_t sector_size; int ready; void *block_node,*char_node; };
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
static int media_read(uint64_t lba,uint32_t count,void*out){
 read_calls++;if(fault_call&&read_calls==fault_call)return EIO;
 assert(count==1&&lba<256);memcpy(out,image+lba*bs,bs);return 0;
}
'''
    bodies = "\n".join(function(source, name) for name in
                         ("b2l_view", "b2l_open", "b2l_strategy", "b2l_write", "b2l_ioctl_common", "b2l_probe_selected"))
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
 bs=block_size;read_calls=fault_call=map_calls=map_error=held=0;memset(image,0,sizeof(image));
 b2l_reader=(struct b2l_ufs_reader){256,bs,media_read,scratch};
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
static void strategy_tests(void){
 fixture(4096);for(size_t i=0;i<sizeof(image);i++)image[i]=(unsigned char)(i*7+i/512);
 memset(b2l_views,0,sizeof(b2l_views));b2l_views[1]=(struct b2l_ufs_view){4096,8192,512,1,NULL,NULL};
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
 assert(b2l_ioctl_common(1,DKIOCGETPHYSICALBLOCKSIZE,(void*)&value,0)==ENOTSUP);
 value=0;assert(!b2l_ioctl_common(1,DKIOCGETBLOCKCOUNT,(void*)&value,0)&&value==16);
 value=4096;assert(!b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,0));
 value=0;assert(!b2l_ioctl_common(1,DKIOCGETBLOCKCOUNT,(void*)&value,0)&&value==2);
 value=1024;assert(b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,0)==EINVAL);
 value=512;assert(b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,1)==ENODEV);
 assert(!b2l_ioctl_common(1,DKIOCSETBLOCKSIZE,(void*)&value,0));
 assert(b2l_ioctl_common(1,999,(void*)&value,0)==ENOTTY&&!held);
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
}
int main(void){gpt_tests();read_tests();strategy_tests();signature_tests();puts("UFS GPT/read-view/BSD strategy/signature tests passed");return 0;}
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
