#include "b2l_cache_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
struct device {int fd;unsigned writes;uint64_t at[8];size_t n[8];};
static int rd(void*v,uint64_t o,void*p,size_t n){return pread(((struct device*)v)->fd,p,n,(off_t)o)==(ssize_t)n?0:-1;}
static int spy_write(void*v,uint64_t o,const void*p,size_t n){(void)p;struct device*d=v;assert(d->writes<8);d->at[d->writes]=o;d->n[d->writes++]=n;return 0;}
static int sync_io(void*v){(void)v;return 0;}
int main(int ac,char**av){
 assert(ac==3);struct device d={.fd=open(av[1],O_RDONLY)};int old=open(av[2],O_RDONLY);assert(d.fd>=0&&old>=0);
 struct b2l_io io={rd,spy_write,sync_io,&d,(uint64_t)lseek(d.fd,0,SEEK_END)};
 struct b2l_hfs*h=calloc(1,sizeof(*h));assert(h&&!b2l_hfs_open(h,&io));struct b2l_hfs_file file;assert(!b2l_hfs_find(h,"b2l-cache.bin",&file));
 struct b2l_cache_fork view_ctx;struct b2l_io view;assert(!b2l_hfs_cache_view(h,&file,&view_ctx,&view));
 uint8_t headers[8192],a[24576],b[24576];const uint8_t*selected;assert(!view.read(view.ctx,0,headers,sizeof(headers)));
 assert(!b2l_nc_headers(headers,headers+24,file.data.size,&selected));unsigned used=b2l_nc_header_used(selected),valid=0;
 for(unsigned i=0;i<used;i++){
  uint64_t off=B2L_NC_HEADER+(uint64_t)i*B2L_NC_CELL;assert(!view.read(view.ctx,off,a,sizeof(a)));assert(pread(old,b,sizeof(b),(off_t)off)==sizeof(b));assert(!memcmp(a,b,sizeof(a)));
  const uint8_t*r;assert(!b2l_nc_select(a,&r));valid+=r!=NULL;
 }
 uint64_t logical=0;unsigned extents=0;
 for(unsigned i=0;i<8&&file.data.count[i];i++){
  extents++;uint64_t bytes=(uint64_t)file.data.count[i]*h->block_size;
  struct b2l_hfs_file bad=file;bad.data.start[i]=h->catalog.start[0];struct b2l_cache_fork bad_ctx;struct b2l_io bad_io;assert(b2l_hfs_cache_view(h,&bad,&bad_ctx,&bad_io));
  if(i+1<8&&file.data.count[i+1]){
   d.writes=0;assert(!view.write(view.ctx,logical+bytes-32,a,64));assert(d.writes==2);
   assert(d.at[0]==((uint64_t)file.data.start[i]+file.data.count[i])*4096-32&&d.n[0]==32);
   assert(d.at[1]==(uint64_t)file.data.start[i+1]*4096&&d.n[1]==32);
  }logical+=bytes;
 }
 d.writes=0;assert(view.write(view.ctx,file.data.size-4,a,8)&&!d.writes);
 assert(view.read(view.ctx,file.data.size-4,a,8));assert(logical==file.data.size);
 printf("Cache layout: %llu bytes, %u slots, %u inline extents, %u/%u original cells byte-identical; cross-extent writes and metadata/out-of-range rejection passed (no writes to image)\n",(unsigned long long)file.data.size,b2l_nc_u32(selected+20),extents,valid,used);
 free(h);close(d.fd);close(old);
}
