/* Exercise the complete source -> undo -> SYSTEM reclamation ordering. */
#include "b2l_nc_store.h"
#include "b2l_nc_undo.h"
#include <assert.h>
#include <stdio.h>
#define SYS_BYTES (B2L_NC_HEADER+2*B2L_NC_CELL)
#define UNDO_BYTES (8192+2*B2L_NC_COPY)
struct media {uint8_t *data;uint64_t capacity,length;};
static uint8_t sys_disk[SYS_BYTES],undo_disk[UNDO_BYTES],root[4096];
static uint8_t sys_saved[SYS_BYTES],undo_saved[UNDO_BYTES];
static struct media sm={sys_disk,SYS_BYTES,SYS_BYTES},um={undo_disk,UNDO_BYTES,8192};
static unsigned writes,syncs,fail_write,fail_sync,prefix;
static int rd(void *ctx,uint64_t off,void *out,size_t n){struct media*m=ctx;if(off>m->length||n>m->length-off)return -1;memcpy(out,m->data+off,n);return 0;}
static int wr(void *ctx,uint64_t off,const void *in,size_t n){struct media*m=ctx;if(off>m->capacity||n>m->capacity-off)return -1;
 unsigned stop=++writes==fail_write;size_t take=stop&&prefix<n?prefix:n;memcpy(m->data+off,in,take);if(off+take>m->length)m->length=off+take;return stop?-1:0;}
static int fs(void*ctx){(void)ctx;return ++syncs==fail_sync?-1:0;}
static uint8_t id[12]={1,2,3},hot[B2L_NC_CELL],record[B2L_NC_COPY],check[B2L_NC_COPY],zero[B2L_NC_COPY],header[4096];
static struct b2l_nc_store store;static struct b2l_nc_undo undo;
static struct b2l_nc_key keys[2];static uint32_t buckets[4],tags[1];
static void reopen(void){
 struct b2l_io sio={rd,wr,fs,&sm,SYS_BYTES},uio={rd,wr,fs,&um,um.length};
 memset(&store,0,sizeof(store));memset(keys,0,sizeof(keys));memset(buckets,0,sizeof(buckets));memset(tags,0,sizeof(tags));
 assert(!b2l_ncs_open(&store,&sio,id));store.keys=keys;store.buckets=buckets;store.bucket_count=4;store.hot=hot;store.tags=tags;store.hot_count=1;
 for(unsigned i=0;i<store.used;i++){const uint8_t*r=NULL;assert(!b2l_ncs_read(&store,i,&r));if(r)assert(!b2l_ncs_index(&store,i,r));}
 assert(!b2l_ncu_open(&undo,&uio,id,1,check));
}
static int transaction(void){
 /* The root write is separately faulted, followed by its synchronous barrier. */
 struct media rm={root,sizeof(root),sizeof(root)};
 if(wr(&rm,0,record+B2L_NC_DATA+4096,4096)||fs(&rm)||memcmp(root,record+B2L_NC_DATA+4096,4096))return -1;
 if(b2l_ncu_append(&undo,record,check))return -1;
 if(b2l_ncs_reclaim(&store,0,zero,check))return -1;
 return b2l_ncs_trim(&store,header,check);
}
static void recover_and_check(void){
 fail_write=fail_sync=0;reopen();const uint8_t*r=NULL;
 assert(!b2l_ncs_read(&store,0,&r));
 /* Any modified source always has a complete published original in one store. */
 if(memcmp(root,record+B2L_NC_DATA,4096))assert(r||undo.count);
 if(r){assert(b2l_nc_page_state(r,root,4096)>=0);memcpy(root,r+B2L_NC_DATA,4096);}
 for(uint64_t i=undo.count;i;i--){assert(!b2l_ncu_read(&undo,i-1,check));assert(b2l_nc_page_state(check,root,4096)>=0);memcpy(root,check+B2L_NC_DATA,4096);}
 assert(!memcmp(root,record+B2L_NC_DATA,4096));
}
int main(void){
 assert(!b2l_nc_header_v2(sys_disk,id,SYS_BYTES,0,1));memcpy(sys_disk+4096,sys_disk,4096);reopen();
 memcpy(record,"B2LNCREC",8);b2l_nc_p64(record+16,42);b2l_nc_p64(record+24,4096);memset(record+40,7,16);b2l_nc_p32(record+56,1);b2l_nc_p32(record+60,4096);
 for(unsigned i=0;i<4096;i+=4){b2l_nc_p32(record+B2L_NC_DATA+i,0xd65f0fff);b2l_nc_p32(record+B2L_NC_DATA+4096+i,0xd65f03c0);}
 const uint8_t*old=NULL;assert(!b2l_ncs_read(&store,0,&old));assert(!b2l_ncs_put(&store,0,record,old,check));
 assert(!b2l_ncs_read(&store,0,&old));assert(!b2l_ncs_put(&store,0,record,old,check));
 memcpy(sys_saved,sys_disk,SYS_BYTES);memcpy(undo_saved,undo_disk,UNDO_BYTES);
 for(unsigned stage=1;stage<=6;stage++)for(unsigned cut=0;cut<B2L_NC_COPY;cut+=127){
  memcpy(sys_disk,sys_saved,SYS_BYTES);memcpy(undo_disk,undo_saved,UNDO_BYTES);um.length=8192;memcpy(root,record+B2L_NC_DATA,4096);writes=syncs=fail_write=fail_sync=0;reopen();
  writes=syncs=0;fail_write=stage;prefix=cut;assert(transaction());recover_and_check();
 }
 for(unsigned stage=1;stage<=6;stage++){
  memcpy(sys_disk,sys_saved,SYS_BYTES);memcpy(undo_disk,undo_saved,UNDO_BYTES);um.length=8192;memcpy(root,record+B2L_NC_DATA,4096);writes=syncs=fail_write=fail_sync=0;reopen();
  writes=syncs=0;fail_sync=stage;assert(transaction());recover_and_check();
 }
 memcpy(sys_disk,sys_saved,SYS_BYTES);memcpy(undo_disk,undo_saved,UNDO_BYTES);um.length=8192;memcpy(root,record+B2L_NC_DATA,4096);writes=syncs=fail_write=fail_sync=0;reopen();assert(!transaction());
 assert(store.live==0&&store.used==0&&undo.count==1);recover_and_check();
 puts("Cross-store reclaim: source/undo/header/dual-erase/trim prefix and sync cuts always retain a published original; restore passed");
}
