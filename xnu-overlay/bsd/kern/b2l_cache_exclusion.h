/* SPDX-License-Identifier: BSD-2-Clause
 * Keep only the integrity-measured corecrypto __TEXT,__text bytes original.
 * Resolve addresses from the main cache image table and the owning subcache's
 * Mach-O/mappings; no release-specific address, UUID, inode or suffix is used.
 * Reader callbacks are bounded and this header is shared with host tests.
 */
#ifndef B2L_CACHE_EXCLUSION_H
#define B2L_CACHE_EXCLUSION_H
#include "b2l_cache_core.h"
struct b2l_nc_image {uint64_t address,size;uint8_t uuid[16];};
struct b2l_nc_exclusion {uint64_t offset,bytes;};
static int b2l_xread(const struct b2l_io*io,uint64_t off,void*out,size_t n){
 return !io||!io->read||off>io->bytes||n>io->bytes-off?-1:io->read(io->ctx,off,out,n);
}
static int b2l_ximage(const struct b2l_io*io,struct b2l_nc_image*out){
 static const char path[]="/usr/lib/system/libcorecrypto.dylib";
 uint8_t h[152],entry[32],name[sizeof(path)];unsigned found=0;
 if(b2l_xread(io,0,h,sizeof(h))||memcmp(h,"dyld_v1",7))return -1;
 uint64_t at=b2l_nc_u64(h+136),count=b2l_nc_u64(h+144);
 if(!count||count>32768||at>io->bytes||count>(io->bytes-at)/32)return -1;
 for(uint64_t i=0;i<count;i++){
  if(b2l_xread(io,at+i*32,entry,sizeof(entry)))return -1;
  uint64_t p=b2l_nc_u32(entry+28);
  if(b2l_xread(io,p,name,sizeof(name)))return -1;
  if(memcmp(name,path,sizeof(path)))continue;
  if(found++)return -1;
  out->address=b2l_nc_u64(entry+16);out->size=b2l_nc_u32(entry+24);memcpy(out->uuid,entry,16);
  if(!out->address||!out->size||out->size>UINT64_MAX-out->address)return -1;
 }
 return found==1?0:-1;
}
/* Translate an unslid VA range within exactly one executable mapping. */
static int b2l_xoffset(const struct b2l_io*io,uint32_t at,uint32_t count,uint64_t va,uint64_t n,uint64_t*out){
 uint8_t m[32];unsigned found=0;
 if(!n||va>UINT64_MAX-n)return -1;
 for(unsigned i=0;i<count;i++){
  if(b2l_xread(io,(uint64_t)at+i*32,m,32))return -1;
  uint64_t a=b2l_nc_u64(m),size=b2l_nc_u64(m+8),off=b2l_nc_u64(m+16);
  if(!size||a>UINT64_MAX-size||off>io->bytes||size>io->bytes-off)return -1;
  if(va>=a&&va-a<size&&n<=size-(va-a)){
   if(found++||!(b2l_nc_u32(m+28)&4))return -1;
   *out=off+va-a;
  }
 }
 return found==1?0:-1;
}
/* 1: owns the measured section; 0: unrelated file; -1: unsafe metadata. */
static int b2l_xresolve(const struct b2l_io*io,const struct b2l_nc_image*image,struct b2l_nc_exclusion*out){
 static const char textseg[16]="__TEXT",textsect[16]="__text";
 uint8_t h[104],m[32],mh[32],lc[72],sec[80],uuid[16];unsigned intersects=0,got_uuid=0,got_text=0;
 if(image->size<32||image->address>UINT64_MAX-image->size||b2l_xread(io,0,h,sizeof(h))||memcmp(h,"dyld_v1",7))return -1;
 uint32_t maps=b2l_nc_u32(h+16),count=b2l_nc_u32(h+20);
 if(!count||count>128||maps>io->bytes||count>(io->bytes-maps)/32)return -1;
 for(unsigned i=0;i<count;i++){
  if(b2l_xread(io,(uint64_t)maps+i*32,m,32))return -1;
  uint64_t a=b2l_nc_u64(m),n=b2l_nc_u64(m+8),off=b2l_nc_u64(m+16);
  if(!n||a>UINT64_MAX-n||off>io->bytes||n>io->bytes-off)return -1;
  if(a<image->address+image->size&&image->address<a+n)intersects++;
 }
 if(!intersects)return 0;
 uint64_t header,commands;
 if(b2l_xoffset(io,maps,count,image->address,32,&header)||b2l_xread(io,header,mh,32)||
    b2l_nc_u32(mh)!=0xfeedfacfu||b2l_nc_u32(mh+4)!=0x100000cu)return -1;
 unsigned nc=b2l_nc_u32(mh+16),nb=b2l_nc_u32(mh+20);
 if(!nc||nc>2048||nb>65536||nb>image->size-32||
    b2l_xoffset(io,maps,count,image->address+32,nb,&commands))return -1;
 uint64_t pos=0,section_va=0,section_bytes=0;
 for(unsigned i=0;i<nc;i++){
  if(pos>nb||nb-pos<8||b2l_xread(io,commands+pos,lc,8))return -1;
  unsigned cmd=b2l_nc_u32(lc),n=b2l_nc_u32(lc+4);
  if(n<8||(n&7)||n>nb-pos)return -1;
  if(cmd==0x1b){
   if(got_uuid++||n!=24||b2l_xread(io,commands+pos,lc,24))return -1;
   memcpy(uuid,lc+8,16);
  }
  if(cmd==0x19){
   if(n<72||b2l_xread(io,commands+pos,lc,72))return -1;
   unsigned ns=b2l_nc_u32(lc+64);if(ns>(n-72)/80)return -1;
   for(unsigned j=0;j<ns;j++){
    if(b2l_xread(io,commands+pos+72+(uint64_t)j*80,sec,80))return -1;
    if(memcmp(sec,textsect,16)||memcmp(sec+16,textseg,16))continue;
    if(got_text++||memcmp(lc+8,textseg,16))return -1;
    section_va=b2l_nc_u64(sec+32);section_bytes=b2l_nc_u64(sec+40);
    uint64_t segva=b2l_nc_u64(lc+24),segn=b2l_nc_u64(lc+32);
    if(section_va<segva||section_va-segva>segn||section_bytes>segn-(section_va-segva))return -1;
   }
  }
  pos+=n;
 }
 if(pos!=nb||got_uuid!=1||got_text!=1||memcmp(uuid,image->uuid,16)||!section_bytes||
    (section_va&3)||(section_bytes&3)||section_va<image->address||section_va-image->address>image->size||
    section_bytes>image->size-(section_va-image->address)||b2l_xoffset(io,maps,count,section_va,section_bytes,&out->offset))return -1;
 if(out->offset&3)return -1;
 out->bytes=section_bytes;return 1;
}
/* Derive a target without changing the durable undo record. Preserve adjacent
 * images on both boundary pages, even when they share the same subcache. */
static unsigned b2l_xfilter(const uint8_t*r,const struct b2l_nc_exclusion*x,uint8_t*out){
 uint64_t off=b2l_nc_u64(r+32);unsigned n=b2l_nc_u32(r+60),words=0;
 memcpy(out,r+B2L_NC_DATA+4096,4096);
 if(!x->bytes||off>UINT64_MAX-n||x->offset>UINT64_MAX-x->bytes)return 0;
 uint64_t start=off>x->offset?off:x->offset,end=off+n<x->offset+x->bytes?off+n:x->offset+x->bytes;
 if(start>=end)return 0;
 for(uint64_t i=start-off;i<end-off;i+=4)if(memcmp(out+i,r+B2L_NC_DATA+i,4))words++;
 memcpy(out+start-off,r+B2L_NC_DATA+start-off,(size_t)(end-start));return words;
}
#endif
