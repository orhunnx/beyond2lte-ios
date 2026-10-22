#include "b2l_cache_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
static int rd(void*v,uint64_t o,void*p,size_t n){return pread(*(int*)v,p,n,(off_t)o)==(ssize_t)n?0:-1;}
int main(int ac,char**av){assert(ac==2);int fd=open(av[1],O_RDONLY);assert(fd>=0);struct b2l_io io={rd,0,0,&fd,(uint64_t)lseek(fd,0,SEEK_END)};struct b2l_hfs*h=calloc(1,sizeof(*h));assert(h);int e=b2l_hfs_open(h,&io);printf("open=%d hfs_bytes=%llu\n",e,(unsigned long long)h->bytes);assert(!e);e=b2l_hfs_journal_empty(h);printf("journal=%d\n",e);assert(!e);struct b2l_hfs_file file;assert(!b2l_hfs_find(h,"b2l-cache.bin",&file));uint64_t at;assert(!b2l_hfs_cache_extent(h,&file,&at));uint8_t headers[8192];assert(!b2l_hfs_read(h,&file,0,headers,sizeof(headers)));assert(!b2l_nc_header(headers,headers+24));assert(!memcmp(headers,headers+4096,4096));printf("Validated contiguous cache file: inode=%u offset=%llu bytes=%llu; metadata excluded\n",file.id,(unsigned long long)at,(unsigned long long)file.data.size);struct b2l_hfs_file bad=file;
 bad.data.start[0]=h->catalog.start[0];assert(b2l_hfs_cache_extent(h,&bad,&at));
 bad=file;bad.data.start[0]=h->jib;assert(b2l_hfs_cache_extent(h,&bad,&at));
 bad=file;bad.data.count[0]++;assert(b2l_hfs_cache_extent(h,&bad,&at));
 bad=file;bad.data.start[1]=1;bad.data.count[1]=1;assert(b2l_hfs_cache_extent(h,&bad,&at));
 bad=file;bad.data.size-=4096;assert(b2l_hfs_cache_extent(h,&bad,&at));
 headers[24]^=1;assert(b2l_nc_header(headers,headers+4096+24));
 puts("HFS metadata/journal overlap, fragmented/wrong-size forks and stale root rejected");
 free(h);close(fd);}
