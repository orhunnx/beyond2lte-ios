/* SPDX-License-Identifier: BSD-2-Clause */
#include "b2l_cache_exclusion.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static uint8_t fixture[65536];
static int memread(void*c,uint64_t o,void*b,size_t n){memcpy(b,(uint8_t*)c+o,n);return 0;}
static int fileread(void*c,uint64_t o,void*b,size_t n){return fseek((FILE*)c,(long)o,SEEK_SET)||fread(b,1,n,(FILE*)c)!=n;}
static struct b2l_io fileio(const char*p){FILE*f=fopen(p,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);return (struct b2l_io){.read=fileread,.ctx=f,.bytes=(uint64_t)n};}
static void setup(void){
 memset(fixture,0,sizeof(fixture));memcpy(fixture,"dyld_v1",7);
 b2l_nc_p32(fixture+16,256);b2l_nc_p32(fixture+20,1);fixture[88]=9;
 b2l_nc_p64(fixture+136,512);b2l_nc_p64(fixture+144,1);
 fixture[512]=7;b2l_nc_p64(fixture+528,0x100000400ULL);b2l_nc_p32(fixture+536,2048);b2l_nc_p32(fixture+540,800);
 memcpy(fixture+800,"/usr/lib/system/libcorecrypto.dylib",sizeof("/usr/lib/system/libcorecrypto.dylib"));
 b2l_nc_p64(fixture+256,0x100000000ULL);b2l_nc_p64(fixture+264,sizeof(fixture));b2l_nc_p32(fixture+284,5);
 uint8_t*p=fixture+1024;b2l_nc_p32(p,0xfeedfacf);b2l_nc_p32(p+4,0x100000c);b2l_nc_p32(p+16,2);b2l_nc_p32(p+20,176);
 p+=32;b2l_nc_p32(p,0x1b);b2l_nc_p32(p+4,24);p[8]=7;
 p+=24;b2l_nc_p32(p,0x19);b2l_nc_p32(p+4,152);memcpy(p+8,"__TEXT",6);
 b2l_nc_p64(p+24,0x100000400ULL);b2l_nc_p64(p+32,2048);b2l_nc_p32(p+64,1);
 p+=72;memcpy(p,"__text",6);memcpy(p+16,"__TEXT",6);b2l_nc_p64(p+32,0x100000700ULL);b2l_nc_p64(p+40,1280);
}
static void synthetic(void){
 setup();struct b2l_io io={.read=memread,.ctx=fixture,.bytes=sizeof(fixture)};struct b2l_nc_image image;struct b2l_nc_exclusion x;
 assert(!b2l_ximage(&io,&image));assert(b2l_xresolve(&io,&image,&x)==1&&x.offset==1792&&x.bytes==1280);
 fixture[1064]^=1;assert(b2l_xresolve(&io,&image,&x)==-1);fixture[1064]^=1;
 memcpy(fixture+288,fixture+256,32);b2l_nc_p32(fixture+20,2);assert(b2l_xresolve(&io,&image,&x)==-1);setup();
 b2l_nc_p32(fixture+284,1);assert(b2l_xresolve(&io,&image,&x)==-1);setup();
 b2l_nc_p64(fixture+264,UINT64_MAX);assert(b2l_xresolve(&io,&image,&x)==-1);setup();
 b2l_nc_p32(fixture+20,129);assert(b2l_xresolve(&io,&image,&x)==-1);setup();
 b2l_nc_p64(fixture+144,UINT64_MAX);assert(b2l_ximage(&io,&image)==-1);setup();assert(!b2l_ximage(&io,&image));
 b2l_nc_p32(fixture+1084,1);assert(b2l_xresolve(&io,&image,&x)==-1);setup();
 b2l_nc_p64(fixture+256,0x200000000ULL);assert(b2l_xresolve(&io,&image,&x)==0);setup();
 b2l_nc_p64(fixture+1192,UINT64_MAX);assert(b2l_xresolve(&io,&image,&x)==-1);setup();
 uint8_t*r=calloc(1,B2L_NC_COPY),target[4096],live[4096];assert(r);
 b2l_nc_p32(r+60,4096);x=(struct b2l_nc_exclusion){4092,8};
 for(unsigned i=0;i<4096;i+=4){b2l_nc_p32(r+B2L_NC_DATA+i,0xd65f0fff);b2l_nc_p32(r+B2L_NC_DATA+4096+i,0xd65f03c0);}
 for(unsigned page=0;page<2;page++){
  b2l_nc_p64(r+32,page*4096);assert(b2l_xfilter(r,&x,target)==1);
  unsigned protected_word=page?0:4092;
  for(unsigned i=0;i<4096;i+=4)assert(b2l_nc_u32(target+i)==(i==protected_word?0xd65f0fffu:0xd65f03c0u));
  /* Any interrupted selective restore remains within original/native bytes. */
  for(unsigned cut=0;cut<=4096;cut++){memcpy(live,r+B2L_NC_DATA+4096,4096);memcpy(live,target,cut);assert(b2l_nc_page_state(r,live,4096)>=0);}
 }
 x=(struct b2l_nc_exclusion){8192,4096};assert(!b2l_xfilter(r,&x,target));assert(!memcmp(target,r+B2L_NC_DATA+4096,4096));free(r);
 puts("Exact module lookup, UUID binding, malformed mappings/commands, boundary preservation and interrupted selective restore passed");
}
static void real(const char*main,const char*sub,const char*cache){
 struct b2l_io m=fileio(main),s=fileio(sub),c=fileio(cache);struct b2l_nc_image image;struct b2l_nc_exclusion x;
 assert(!b2l_ximage(&m,&image));assert(b2l_xresolve(&s,&image,&x)==1);
 uint8_t uuid[16];assert(!b2l_xread(&s,88,uuid,16));uint8_t*cell=malloc(B2L_NC_CELL),target[4096];assert(cell);
 unsigned words=0,pages=0,others=0;uint8_t*pristine=malloc((size_t)x.bytes);assert(pristine);assert(!b2l_xread(&s,x.offset,pristine,(size_t)x.bytes));
 for(unsigned i=0;i<B2L_NC_SLOTS;i++){
  assert(!b2l_xread(&c,B2L_NC_HEADER+(uint64_t)i*B2L_NC_CELL,cell,B2L_NC_CELL));const uint8_t*r=NULL;assert(!b2l_nc_select(cell,&r));
  if(!r||b2l_nc_u32(r+56)!=2||memcmp(r+40,uuid,16))continue;
  unsigned n=b2l_xfilter(r,&x,target);words+=n;if(n)pages++;
  uint64_t off=b2l_nc_u64(r+32);unsigned len=b2l_nc_u32(r+60);
  for(unsigned j=0;j<len;j++){
   if(off+j>=x.offset&&off+j-x.offset<x.bytes){assert(target[j]==r[B2L_NC_DATA+j]);assert(target[j]==pristine[off+j-x.offset]);}
   else {assert(target[j]==r[B2L_NC_DATA+4096+j]);if(target[j]!=r[B2L_NC_DATA+j])others++;}
  }
 }
 printf("Private cache: measured bytes=%llu, excluded words=%u in %u pages; adjacent changed bytes preserved=%u\n",(unsigned long long)x.bytes,words,pages,others);
 assert(words>0&&pages>0);free(pristine);free(cell);fclose(m.ctx);fclose(s.ctx);fclose(c.ctx);
}
int main(int argc,char**argv){synthetic();if(argc==4)real(argv[1],argv[2],argv[3]);else assert(argc==1);}
