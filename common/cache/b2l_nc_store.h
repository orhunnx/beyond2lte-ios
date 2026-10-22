/* SPDX-License-Identifier: BSD-2-Clause
 * Bounded hot cells, disk-backed original/native records and an in-RAM key
 * index. Caller serializes access; no allocation, filesystem or scheduler API.
 */
#ifndef B2L_NC_STORE_H
#define B2L_NC_STORE_H
#include "b2l_cache_core.h"
struct b2l_nc_key {uint64_t inode,offset;};
struct b2l_nc_store {
 struct b2l_io io;uint8_t id[12],headers[B2L_NC_HEADER];
 struct b2l_nc_key*keys;uint32_t*buckets,*tags;
 uint8_t*hot;uint32_t slots,used,next_free,bucket_count,hot_count;
 unsigned header_copy;uint64_t generation;
};
static inline uint32_t b2l_ncs_hash(uint64_t ino,uint64_t off){
 uint64_t x=ino^(off>>12);x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;x^=x>>31;return (uint32_t)x;
}
static inline uint32_t b2l_ncs_find(struct b2l_nc_store*s,uint64_t ino,uint64_t off){
 uint32_t h=b2l_ncs_hash(ino,off)&(s->bucket_count-1);
 for(uint32_t n=0;n<s->bucket_count;n++,h=(h+1)&(s->bucket_count-1)){
  uint32_t v=s->buckets[h];if(!v)return s->slots;
  if(s->keys[v-1].inode==ino&&s->keys[v-1].offset==off)return v-1;
 }return s->slots;
}
static inline int b2l_ncs_index(struct b2l_nc_store*s,uint32_t i,const uint8_t*r){
 uint64_t ino=b2l_nc_u64(r+16),off=b2l_nc_u64(r+32);
 if(i>=s->slots||!ino||s->keys[i].inode||b2l_ncs_find(s,ino,off)!=s->slots)return -1;
 uint32_t h=b2l_ncs_hash(ino,off)&(s->bucket_count-1);
 for(uint32_t n=0;n<s->bucket_count;n++,h=(h+1)&(s->bucket_count-1))if(!s->buckets[h]){
  s->keys[i]=(struct b2l_nc_key){ino,off};s->buckets[h]=i+1;
  while(s->next_free<s->slots&&s->keys[s->next_free].inode)s->next_free++;
  return 0;
 }
 return -1;
}
static inline int b2l_ncs_read(struct b2l_nc_store*s,uint32_t i,const uint8_t**r){
 *r=NULL;if(i>=s->slots||!s->hot_count)return -1;uint32_t h=i%s->hot_count;
 uint8_t*cell=s->hot+(uint64_t)h*B2L_NC_CELL;
 if(s->tags[h]!=i+1){
  /* Invalidate before I/O so a short/failed replacement is never reused. */
  s->tags[h]=0;
  if(s->io.read(s->io.ctx,B2L_NC_HEADER+(uint64_t)i*B2L_NC_CELL,cell,B2L_NC_CELL))return -1;
  s->tags[h]=i+1;
 }
 return b2l_nc_select(cell,r);
}
static inline int b2l_ncs_open(struct b2l_nc_store*s,const struct b2l_io*io,const uint8_t id[12]){
 const uint8_t*h=NULL;
 if(!io||!io->read||!io->write||!io->sync||io->read(io->ctx,0,s->headers,B2L_NC_HEADER)||
    b2l_nc_headers(s->headers,id,io->bytes,&h))return -1;
 s->io=*io;memcpy(s->id,id,12);s->slots=b2l_nc_u32(h+20);s->used=b2l_nc_header_used(h);
 s->generation=b2l_nc_header_generation(h);s->header_copy=h==s->headers?0:1;return 0;
}
static inline int b2l_ncs_put(struct b2l_nc_store*s,uint32_t i,uint8_t*r,const uint8_t*old,uint8_t*verify){
 if(i>=s->slots||!s->hot_count)return -1;
 uint32_t h=i%s->hot_count;uint8_t*cell=s->hot+(uint64_t)h*B2L_NC_CELL;
 if(s->tags[h]!=i+1||(old&&old!=cell&&old!=cell+B2L_NC_COPY))return -1;
 uint64_t seq=old?b2l_nc_u64(old+8):0;if(seq==~(uint64_t)0)return -1;
 unsigned copy=old==cell?1:0;uint64_t at=B2L_NC_HEADER+(uint64_t)i*B2L_NC_CELL+copy*B2L_NC_COPY;
 b2l_nc_p64(r+8,seq+1);b2l_nc_seal(r);if(b2l_nc_record(r))return -1;
 if(s->io.write(s->io.ctx,at,r,B2L_NC_COPY)||s->io.sync(s->io.ctx)||
    s->io.read(s->io.ctx,at,verify,B2L_NC_COPY)||memcmp(r,verify,B2L_NC_COPY))return -1;
 if(i>=s->used){
  uint8_t next[4096];unsigned hc=s->header_copy^1;
  if(s->generation==~(uint64_t)0||b2l_nc_u32(s->headers+s->header_copy*4096+8)!=2||
     b2l_nc_header_v2(next,s->id,s->io.bytes,i+1,s->generation+1))return -1;
  /* A new high-water mark becomes durable only after its record is durable.
   * A crash before this publication may lose learning, never an applied undo. */
  if(s->io.write(s->io.ctx,hc*4096,next,4096)||s->io.sync(s->io.ctx)||
     s->io.read(s->io.ctx,hc*4096,verify,4096)||memcmp(next,verify,4096))return -1;
  memcpy(s->headers+hc*4096,next,4096);s->header_copy=hc;s->generation++;s->used=i+1;
 }
 memcpy(cell+copy*B2L_NC_COPY,r,B2L_NC_COPY);
 if(!s->keys[i].inode)return b2l_ncs_index(s,i,r);
 return s->keys[i].inode==b2l_nc_u64(r+16)&&s->keys[i].offset==b2l_nc_u64(r+32)?0:-1;
}
#endif
