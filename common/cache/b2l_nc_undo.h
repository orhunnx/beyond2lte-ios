/* SPDX-License-Identifier: BSD-2-Clause
 * Append-only undo archive. A record is durable before its alternate A/B
 * header publishes it. Uncommitted tails are ignored/reused after a cut.
 */
#ifndef B2L_NC_UNDO_H
#define B2L_NC_UNDO_H
#include "b2l_cache_core.h"
struct b2l_nc_undo {struct b2l_io io;uint8_t id[12],headers[8192],next[4096];
 uint64_t count,generation;unsigned copy;};
static inline void b2l_ncu_header(uint8_t*h,const uint8_t id[12],uint64_t count,uint64_t gen){
 memset(h,0,4096);memcpy(h,"B2LUNDO1",8);memcpy(h+8,id,12);
 b2l_nc_p64(h+24,gen);b2l_nc_p64(h+32,count);b2l_nc_p32(h+60,b2l_nc_crc(h,60));
}
static inline int b2l_ncu_valid(const uint8_t*h,const uint8_t id[12],uint64_t bytes){
 if(bytes<8192||memcmp(h,"B2LUNDO1",8)||memcmp(h+8,id,12)||!b2l_nc_u64(h+24)||
    b2l_nc_u64(h+32)>(bytes-8192)/B2L_NC_COPY||b2l_nc_u32(h+60)!=b2l_nc_crc(h,60))return -1;
 for(unsigned i=20;i<24;i++)if(h[i])return -1;
 for(unsigned i=40;i<60;i++)if(h[i])return -1;
 for(unsigned i=64;i<4096;i++)if(h[i])return -1;
 return 0;
}
static inline int b2l_ncu_open(struct b2l_nc_undo*u,const struct b2l_io*io,
    const uint8_t id[12],int create,uint8_t*verify){
 u->io=*io;memcpy(u->id,id,12);memset(u->headers,0,8192);
 size_t n=io->bytes<8192?(size_t)io->bytes:8192;
 if(n&&io->read(io->ctx,0,u->headers,n))return -1;
 int a=!b2l_ncu_valid(u->headers,id,io->bytes),b=!b2l_ncu_valid(u->headers+4096,id,io->bytes);
 if(!a&&!b){
  if(!create||io->bytes>8192)return -1;
  /* Only a zero/expected empty-archive initialization prefix is resumable. */
  b2l_ncu_header(u->next,id,0,1);
  for(size_t i=0;i<n;i++)if(u->headers[i]&&u->headers[i]!=u->next[i%4096])return -1;
  memcpy(u->headers,u->next,4096);memcpy(u->headers+4096,u->next,4096);
  if(io->write(io->ctx,0,u->headers,8192)||io->sync(io->ctx)||
     io->read(io->ctx,0,verify,8192)||memcmp(verify,u->headers,8192))return -1;
  u->io.bytes=8192;a=b=1;
 }
 uint64_t ga=a?b2l_nc_u64(u->headers+24):0,gb=b?b2l_nc_u64(u->headers+4096+24):0;
 if(a&&b&&ga==gb&&memcmp(u->headers,u->headers+4096,4096))return -1;
 u->copy=ga>=gb?0:1;const uint8_t*h=u->headers+u->copy*4096;
 u->count=b2l_nc_u64(h+32);u->generation=b2l_nc_u64(h+24);return 0;
}
static inline int b2l_ncu_read(struct b2l_nc_undo*u,uint64_t i,uint8_t*r){
 if(i>=u->count||i>(INT64_MAX-8192)/B2L_NC_COPY)return -1;
 return u->io.read(u->io.ctx,8192+i*B2L_NC_COPY,r,B2L_NC_COPY)||b2l_nc_record(r)?-1:0;
}
static inline int b2l_ncu_append(struct b2l_nc_undo*u,const uint8_t*r,uint8_t*verify){
 if(b2l_nc_record(r)||u->generation==UINT64_MAX||u->count>=(INT64_MAX-8192)/B2L_NC_COPY)return -1;
 uint64_t at=8192+u->count*B2L_NC_COPY;
 if(u->io.write(u->io.ctx,at,r,B2L_NC_COPY)||u->io.sync(u->io.ctx)||
    u->io.read(u->io.ctx,at,verify,B2L_NC_COPY)||memcmp(r,verify,B2L_NC_COPY))return -1;
 unsigned copy=u->copy^1;b2l_ncu_header(u->next,u->id,u->count+1,u->generation+1);
 if(u->io.write(u->io.ctx,copy*4096,u->next,4096)||u->io.sync(u->io.ctx)||
    u->io.read(u->io.ctx,copy*4096,verify,4096)||memcmp(u->next,verify,4096))return -1;
 memcpy(u->headers+copy*4096,u->next,4096);u->copy=copy;u->generation++;u->count++;
 if(at+B2L_NC_COPY>u->io.bytes)u->io.bytes=at+B2L_NC_COPY;return 0;
}
#endif
