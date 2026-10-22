#include "b2l_nc_store.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#define CELLS 16u
#define BYTES (B2L_NC_HEADER+CELLS*B2L_NC_CELL)
static uint8_t disk[BYTES],saved[BYTES],id[12]={1,2,3};
static int writes,fail_write,short_write,syncs,fail_sync,bad_read;
static int rd(void*v,uint64_t at,void*out,size_t n){(void)v;if(at>BYTES||n>BYTES-at)return -1;memcpy(out,disk+at,n);if(bad_read&&writes)((uint8_t*)out)[n-1]^=1;return 0;}
static int wr(void*v,uint64_t at,const void*in,size_t n){(void)v;assert(at<=BYTES&&n<=BYTES-at);writes++;if(writes==fail_write){memcpy(disk+at,in,(size_t)short_write<n?(size_t)short_write:n);return -1;}memcpy(disk+at,in,n);return 0;}
static int sync_io(void*v){(void)v;return ++syncs==fail_sync?-1:0;}
static struct b2l_io io={rd,wr,sync_io,0,BYTES};
static struct b2l_nc_store store;
static struct b2l_nc_key keys[CELLS];static uint32_t buckets[32],tags[2];static uint8_t hot[2*B2L_NC_CELL];
static uint8_t record[B2L_NC_COPY],verify[B2L_NC_COPY];
static void bind_store(void){store.keys=keys;store.buckets=buckets;store.bucket_count=32;store.hot=hot;store.tags=tags;store.hot_count=2;}
static void open_store(void){memset(&store,0,sizeof(store));memset(keys,0,sizeof(keys));memset(buckets,0,sizeof(buckets));memset(tags,0,sizeof(tags));assert(!b2l_ncs_open(&store,&io,id));bind_store();for(unsigned i=0;i<store.used;i++){const uint8_t*r;assert(!b2l_ncs_read(&store,i,&r));if(r)assert(!b2l_ncs_index(&store,i,r));}}
static void initialize(void){memset(disk,0,sizeof(disk));writes=syncs=fail_write=fail_sync=short_write=bad_read=0;assert(!b2l_nc_header_v2(disk,id,BYTES,0,1));memcpy(disk+4096,disk,4096);open_store();}
static void make_record(unsigned i){memset(record,0,sizeof(record));memcpy(record,"B2LNCREC",8);b2l_nc_p64(record+16,42);b2l_nc_p64(record+24,CELLS*4096);b2l_nc_p64(record+32,i*4096);memset(record+40,7,16);b2l_nc_p32(record+56,1);b2l_nc_p32(record+60,4096);b2l_nc_p32(record+B2L_NC_DATA,0xd65f0fff);b2l_nc_p32(record+B2L_NC_DATA+4096,0xd65f03c0);}
static int put(unsigned i){const uint8_t*r;assert(!b2l_ncs_read(&store,i,&r));make_record(i);return b2l_ncs_put(&store,i,record,r,verify);}
int main(void){
 uint8_t headers[8192],next[4096];const uint8_t*h;
 assert(!b2l_nc_header_v2(headers,id,6ULL*1024*1024*1024,5461,1));memcpy(headers+4096,headers,4096);
 assert(!b2l_nc_headers(headers,id,6ULL*1024*1024*1024,&h)&&b2l_nc_header_used(h)==5461);
 assert(b2l_nc_headers(headers,id,128ULL*1024*1024,&h));
 assert(!b2l_nc_header_v2(next,id,6ULL*1024*1024*1024,5462,2));
 for(unsigned cut=0;cut<4096;cut++){memcpy(headers+4096,headers,4096);memcpy(headers+4096,next,cut);assert(!b2l_nc_headers(headers,id,6ULL*1024*1024*1024,&h));assert(b2l_nc_header_used(h)==5461||b2l_nc_header_used(h)==5462);}
 memcpy(headers+4096,next,4096);assert(!b2l_nc_headers(headers,id,6ULL*1024*1024*1024,&h)&&b2l_nc_header_used(h)==5462);
 b2l_nc_p64(headers+4096+44,1);b2l_nc_p32(headers+4096+60,b2l_nc_crc(headers+4096,60));assert(b2l_nc_headers(headers,id,6ULL*1024*1024*1024,&h));
 initialize();for(unsigned i=0;i<CELLS;i++){assert(store.next_free==i);assert(!put(i));}assert(store.used==CELLS&&store.next_free==CELLS);
 // Sixteen durable pages survive eviction through a two-cell RAM window.
 open_store();for(unsigned i=0;i<CELLS;i++){assert(b2l_ncs_find(&store,42,i*4096)==i);const uint8_t*r;assert(!b2l_ncs_read(&store,i,&r)&&r);assert(b2l_nc_u32(r+B2L_NC_DATA)==0xd65f0fff&&b2l_nc_u32(r+B2L_NC_DATA+4096)==0xd65f03c0);}
 assert(b2l_ncs_find(&store,99,0)==CELLS);
 initialize();assert(!put(0));memcpy(saved,disk,sizeof(disk));
 // Interrupted record or high-water publication preserves the previous page;
 // any published new page has an independently valid durable record.
 for(unsigned which=1;which<=2;which++)for(unsigned cut=0;cut<(which==1?B2L_NC_COPY:4096);cut+=127){
  memcpy(disk,saved,sizeof(disk));writes=syncs=0;fail_write=(int)which;short_write=(int)cut;open_store();assert(put(1));fail_write=0;open_store();const uint8_t*r;assert(!b2l_ncs_read(&store,0,&r)&&r);if(store.used==2)assert(!b2l_ncs_read(&store,1,&r)&&r);
 }
 for(int stage=1;stage<=2;stage++){
  memcpy(disk,saved,sizeof(disk));writes=syncs=0;fail_sync=stage;open_store();assert(put(1));fail_sync=0;open_store();const uint8_t*r;assert(!b2l_ncs_read(&store,0,&r)&&r);
 }
 puts("V2 64-bit capacity, all torn-header prefixes, bounded RAM eviction, disk reindex, and interrupted record/header commits passed");
}
