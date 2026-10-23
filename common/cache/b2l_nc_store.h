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
 uint8_t*hot;uint32_t slots,used,next_free,bucket_count,hot_count,live;
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
  s->keys[i]=(struct b2l_nc_key){ino,off};s->buckets[h]=i+1;s->live++;
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
/* Caller must first fsync/read back the root page and publish a durable undo
 * record elsewhere (or prove target == original). Erase the older copy first:
 * a cut leaves either the latest record or a reusable cell, never an old undo.
 * Index publication happens only after BOTH erasures are flushed/read back. */
static inline int b2l_ncs_reclaim(struct b2l_nc_store*s,uint32_t i,
    const uint8_t*zero,uint8_t*verify){
 const uint8_t*old=NULL;
 if(i>=s->used||!s->keys[i].inode||b2l_ncs_read(s,i,&old)||!old)return -1;
 for(unsigned n=0;n<B2L_NC_COPY;n++)if(zero[n])return -1;
 uint8_t*cell=s->hot+(uint64_t)(i%s->hot_count)*B2L_NC_CELL;
 unsigned latest=old==cell?0:1;
 for(unsigned pass=0;pass<2;pass++){
  unsigned copy=pass?latest:latest^1;
  uint64_t at=B2L_NC_HEADER+(uint64_t)i*B2L_NC_CELL+(uint64_t)copy*B2L_NC_COPY;
  if(s->io.write(s->io.ctx,at,zero,B2L_NC_COPY)||s->io.sync(s->io.ctx)||
     s->io.read(s->io.ctx,at,verify,B2L_NC_COPY)||memcmp(verify,zero,B2L_NC_COPY)){
   s->tags[i%s->hot_count]=0;return -1;
  }
 }
 uint32_t mask=s->bucket_count-1,h=b2l_ncs_hash(s->keys[i].inode,s->keys[i].offset)&mask;
 while(s->buckets[h]&&s->buckets[h]!=i+1)h=(h+1)&mask;
 if(s->buckets[h]!=i+1)return -1;
 s->buckets[h]=0;
 /* Back-shift by reinserting the following linear-probe cluster. */
 for(uint32_t j=(h+1)&mask;s->buckets[j];j=(j+1)&mask){
  uint32_t v=s->buckets[j];s->buckets[j]=0;
  uint32_t k=b2l_ncs_hash(s->keys[v-1].inode,s->keys[v-1].offset)&mask;
  while(s->buckets[k])k=(k+1)&mask;s->buckets[k]=v;
 }
 s->keys[i]=(struct b2l_nc_key){0,0};s->live--;
 if(i<s->next_free)s->next_free=i;
 memset(cell,0,B2L_NC_CELL);return 0;
}
/* Shrink only the committed scan range. Slot allocation itself is unchanged.
 * Falling back to an older header merely scans additional erased cells. */
static inline int b2l_ncs_trim(struct b2l_nc_store*s,uint8_t*next,uint8_t*verify){
 uint32_t used=s->used;while(used&&!s->keys[used-1].inode)used--;
 if(used==s->used)return 0;
 unsigned copy=s->header_copy^1;
 if(s->generation==UINT64_MAX||b2l_nc_u32(s->headers+s->header_copy*4096+8)!=2||
    b2l_nc_header_v2(next,s->id,s->io.bytes,used,s->generation+1))return -1;
 if(s->io.write(s->io.ctx,copy*4096,next,4096)||s->io.sync(s->io.ctx)||
    s->io.read(s->io.ctx,copy*4096,verify,4096)||memcmp(next,verify,4096))return -1;
 memcpy(s->headers+copy*4096,next,4096);s->header_copy=copy;s->generation++;s->used=used;return 0;
}
#endif
