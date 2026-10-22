/* SPDX-License-Identifier: BSD-2-Clause
 * Bounded HFS+/HFSX existing-data-fork access. No metadata/file creation.
 * Overflow extents, compressed/sparse forks and non-ASCII paths fail closed.
 */
#include "CacheCore.h"
static uint16_t b16(const uint8_t*p){return (uint16_t)p[0]<<8|p[1];}
static uint32_t b32(const uint8_t*p){return (uint32_t)b16(p)<<16|b16(p+2);}
static uint64_t b64(const uint8_t*p){return (uint64_t)b32(p)<<32|b32(p+4);}
static uint32_t l32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t l64(const uint8_t*p){return (uint64_t)l32(p)|(uint64_t)l32(p+4)<<32;}
static int fork_decode(struct b2l_hfs*h,const uint8_t*p,struct b2l_fork*f){
 uint64_t cap=0;int ended=0;f->size=b64(p);
 for(unsigned i=0;i<8;i++){uint32_t a=b32(p+16+8*i),n=b32(p+20+8*i);f->start[i]=a;f->count[i]=n;if(!n){if(a)return -1;ended=1;continue;}if(ended||!a||a>=h->blocks||n>h->blocks-a)return -1;for(unsigned j=0;j<i;j++)if(n&&f->count[j]&&!(a+n<=f->start[j]||f->start[j]+f->count[j]<=a))return -1;cap+=(uint64_t)n*h->block_size;}
 return f->size<=cap?0:-1;
}
static int fork_io(struct b2l_hfs*h,const struct b2l_fork*f,uint64_t off,void*p,size_t n){
 if(off>f->size||n>f->size-off)return -1;
 for(unsigned i=0;i<8&&n;i++){uint64_t len=(uint64_t)f->count[i]*h->block_size;if(off>=len){off-=len;continue;}size_t take=n;if((uint64_t)take>len-off)take=(size_t)(len-off);uint64_t pa=(uint64_t)f->start[i]*h->block_size+off;
  if(pa>h->bytes||take>h->bytes-pa)return -1;
  int e=h->io.read(h->io.ctx,pa,p,take);if(e)return -3;p=(uint8_t*)p+take;n-=take;off=0;}
 return n?-1:0;
}
int b2l_hfs_read(struct b2l_hfs*h,const struct b2l_hfs_file*f,uint64_t off,void*p,size_t n){return fork_io(h,&f->data,off,p,n);}
int b2l_hfs_open(struct b2l_hfs*h,const struct b2l_io*io){
 uint8_t p[512],a[512];if(!h||!io||!io->read||io->bytes<4096)return -1;memset(h,0,sizeof(*h));h->io=*io;
 if(io->read(io->ctx,1024,p,sizeof(p)))return -3;
 if(!((p[0]=='H'&&p[1]=='+'&&b16(p+2)==4)||(p[0]=='H'&&p[1]=='X'&&b16(p+2)==5)))return -1;
 h->block_size=b32(p+40);h->blocks=b32(p+44);h->create_date=b32(p+16);h->jib=b32(p+12);memcpy(h->uuid,p+104,8);
 if(h->block_size<512||h->block_size>65536||(h->block_size&(h->block_size-1))||!h->blocks)return -1;
 h->bytes=(uint64_t)h->blocks*h->block_size;
 if(h->bytes>io->bytes||h->bytes<4096||!(b32(p+4)&(1u<<13))||h->jib>=h->blocks||!h->jib)return -1;
 if(io->read(io->ctx,h->bytes-1024,a,sizeof(a)))return -3;
 if(memcmp(p,a,4)||memcmp(p+16,a+16,4)||memcmp(p+40,a+40,8)||memcmp(p+104,a+104,8))return -1;
 return fork_decode(h,p+272,&h->catalog);
}
int b2l_hfs_journal_empty(struct b2l_hfs*h){
 uint8_t j[512],b[512];if(h->io.read(h->io.ctx,(uint64_t)h->jib*h->block_size,b,512))return -3;
 uint64_t off=b64(b+36),size=b64(b+44);if(b32(b)!=1||size<512||off>h->bytes||size>h->bytes-off||off%512)return -1;
 if(h->io.read(h->io.ctx,off,j,512))return -3;
 if(l32(j)!=0x4a4e4c78||l32(j+4)!=0x12345678||l64(j+24)!=size)return -1;
 uint32_t saved=l32(j+36),sum=0;memset(j+36,0,4);for(unsigned i=0;i<44;i++)sum=(sum<<8)^(sum+j[i]);if(~sum!=saved)return -1;
 uint64_t start=l64(j+8),end=l64(j+16);uint32_t hdr=l32(j+40);if(hdr<512||hdr>65536||(hdr&(hdr-1))||hdr>size||start<hdr||end<hdr||start>=size||end>=size||start%512||end%512)return -1;
 return start==end?0:-2;
}
int b2l_hfs_find(struct b2l_hfs*h,const char*path,struct b2l_hfs_file*out){
 uint8_t hdr[512];char name[256];uint32_t parent=2;unsigned parts=0;
 if(!path||!*path||*path=='/'||!out)return -1;
 while(*path){unsigned len=0;while(*path&&*path!='/'){unsigned char c=(unsigned char)*path++;if(c<32||c>126||len>=255)return -1;name[len++]=(char)c;}name[len]=0;
  int last=!*path;if(!last){path++;if(!*path)return -1;}if(!len||!strcmp(name,".")||!strcmp(name,"..")||++parts>16)return -1;
  if(fork_io(h,&h->catalog,0,hdr,sizeof(hdr)))return -3;
  uint32_t leaf=b32(hdr+24),total=b32(hdr+36),previous=0;uint16_t ns=b16(hdr+32);int found=0;
  if(hdr[8]!=1||ns<512||ns>B2L_HFS_NODE_MAX||(ns&(ns-1))||!total||(uint64_t)total*ns>h->catalog.size)return -1;
  for(unsigned hop=0;leaf&&hop<total&&hop<16384;hop++){
   if(leaf>=total)return -1;uint8_t*d=h->node;if(fork_io(h,&h->catalog,(uint64_t)leaf*ns,d,ns))return -3;
   unsigned nr=b16(d+10);if(d[8]!=255||d[9]!=1||b32(d+4)!=previous||nr>(ns-14)/4)return -1;
   for(unsigned i=0;i<nr;i++){unsigned at=b16(d+ns-2*(i+1)),end=b16(d+ns-2*(i+2));if(at<14||end<=at||end>ns-2*(nr+1)||end-at<8)return -1;
    unsigned kl=b16(d+at),chars=b16(d+at+6);if(chars>255||kl!=6+chars*2||2+kl>end-at)return -1;
    if(b32(d+at+2)!=parent||chars!=len)continue;unsigned k;for(k=0;k<len;k++)if(b16(d+at+8+2*k)!=(unsigned char)name[k])break;if(k!=len)continue;
    uint8_t *rec=d+at+2+kl;unsigned recsize=end-at-2-kl;uint16_t type=recsize>=2?b16(rec):0;
    if(last){if(type!=2||recsize<248||((b16(rec+42)&0170000)!=0100000)||(b32(rec+44)&0x40000020u))return -1;out->id=b32(rec+8);return fork_decode(h,rec+88,&out->data);}
    if(type!=1||recsize<88)return -1;parent=b32(rec+8);found=1;break;
   }
   if(found)break;previous=leaf;leaf=b32(d);
  }
  if(!found)return -1;
 }
 return -1;
}

/* Authorize one fully allocated contiguous cache data fork, excluding every
 * volume metadata fork, volume headers, JIB and internal journal. */
int b2l_hfs_cache_extent(struct b2l_hfs*h,const struct b2l_hfs_file*f,uint64_t*out){
 uint8_t vh[512],jib[512];
 if(!h||!f||!out||h->block_size!=4096||f->data.size!=128u*1024u*1024u||
    !f->data.start[0]||f->data.count[0]!=f->data.size/h->block_size)return -1;
 for(unsigned i=1;i<8;i++)if(f->data.start[i]||f->data.count[i])return -1;
 if(h->io.read(h->io.ctx,1024,vh,512)||h->io.read(h->io.ctx,(uint64_t)h->jib*h->block_size,jib,512))return -3;
 if(!(b32(vh+4)&(1u<<8))||(b32(vh+4)&(1u<<11))||b2l_hfs_journal_empty(h))return -1;
 uint64_t a=(uint64_t)f->data.start[0]*h->block_size,n=f->data.size,b=b64(jib+36),z=b64(jib+44);
 if(a<4096||a>h->bytes||n>h->bytes-a||a+n>h->bytes-4096||
    !(a+n<=b||b+z<=a)||!((uint64_t)(h->jib+1)*h->block_size<=a||a+n<=(uint64_t)h->jib*h->block_size))return -1;
 for(unsigned k=0;k<5;k++){struct b2l_fork meta;
  if(fork_decode(h,vh+112+k*80,&meta))return -1;
  for(unsigned i=0;i<8;i++){b=(uint64_t)meta.start[i]*h->block_size;z=(uint64_t)meta.count[i]*h->block_size;
   if(z&&!(a+n<=b||b+z<=a))return -1;}}
 *out=a;return 0;
}
