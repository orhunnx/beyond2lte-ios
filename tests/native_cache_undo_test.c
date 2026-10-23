#include "b2l_nc_undo.h"
#include <assert.h>
#include <stdio.h>
#define CAP (8192+8*B2L_NC_COPY)
static uint8_t disk[CAP],saved[CAP],id[12]={1,2,3},r[B2L_NC_COPY],v[B2L_NC_COPY];
static uint64_t length,saved_length;static unsigned writes,syncs,fail_write,cut,fail_sync;
static int read_io(void*c,uint64_t at,void*out,size_t n){(void)c;if(at>length||n>length-at)return -1;memcpy(out,disk+at,n);return 0;}
static int write_io(void*c,uint64_t at,const void*in,size_t n){(void)c;if(at>CAP||n>CAP-at)return -1;
 writes++;size_t take=writes==fail_write&&cut<n?cut:n;memcpy(disk+at,in,take);if(at+take>length)length=at+take;return writes==fail_write?-1:0;}
static int sync_io(void*c){(void)c;return ++syncs==fail_sync?-1:0;}
static struct b2l_nc_undo undo;
static int open_undo(void){struct b2l_io io={read_io,write_io,sync_io,0,length};return b2l_ncu_open(&undo,&io,id,1,v);}
static void make_record(void){memset(r,0,sizeof(r));memcpy(r,"B2LNCREC",8);b2l_nc_p64(r+8,1);b2l_nc_p64(r+16,42);b2l_nc_p64(r+24,4096);memset(r+40,7,16);b2l_nc_p32(r+56,1);b2l_nc_p32(r+60,4096);b2l_nc_p32(r+B2L_NC_DATA,0xd65f0fff);b2l_nc_p32(r+B2L_NC_DATA+4096,0xd65f03c0);b2l_nc_seal(r);assert(!b2l_nc_record(r));}
int main(void){
 assert(!open_undo()&&undo.count==0);make_record();assert(!b2l_ncu_append(&undo,r,v));assert(!open_undo()&&undo.count==1);assert(!b2l_ncu_read(&undo,0,v)&&!memcmp(v,r,sizeof(r)));
 memcpy(saved,disk,CAP);saved_length=length;
 // Record/header prefix cuts never publish an invalid undo record. Retry
 // resumes at the old high-water mark or after an already committed append.
 for(unsigned stage=1;stage<=2;stage++)for(unsigned p=0;p<(stage==1?B2L_NC_COPY:4096);p+=127){
  memcpy(disk,saved,CAP);length=saved_length;writes=syncs=0;fail_write=stage;cut=p;assert(!open_undo());assert(b2l_ncu_append(&undo,r,v));fail_write=0;
  assert(!open_undo()&&(undo.count==1||undo.count==2));for(uint64_t i=0;i<undo.count;i++)assert(!b2l_ncu_read(&undo,i,v));
  assert(!b2l_ncu_append(&undo,r,v));
 }
 for(unsigned stage=1;stage<=2;stage++){
  memcpy(disk,saved,CAP);length=saved_length;writes=syncs=0;fail_sync=stage;assert(!open_undo());assert(b2l_ncu_append(&undo,r,v));fail_sync=0;
  assert(!open_undo());for(uint64_t i=0;i<undo.count;i++)assert(!b2l_ncu_read(&undo,i,v));
 }
 // Empty-archive creation is resumable; a different identity is rejected.
 for(unsigned p=0;p<8192;p+=127){memset(disk,0,CAP);length=0;writes=syncs=0;fail_write=1;cut=p;assert(open_undo());fail_write=0;assert(!open_undo()&&undo.count==0);}
 id[0]^=1;assert(open_undo());id[0]^=1;
 // Original/native page chains must be undone in reverse append order.
 memset(disk,0,CAP);length=0;writes=syncs=0;assert(!open_undo());make_record();
 b2l_nc_p32(r+B2L_NC_DATA+4,0xd503237f);b2l_nc_p32(r+B2L_NC_DATA+4096+4,0xd503237f);b2l_nc_seal(r);assert(!b2l_ncu_append(&undo,r,v));
 uint8_t page[4096];memcpy(page,r+B2L_NC_DATA+4096,4096);memcpy(r+B2L_NC_DATA,page,4096);b2l_nc_p32(r+B2L_NC_DATA+4096+4,0xd503201f);b2l_nc_seal(r);assert(!b2l_ncu_append(&undo,r,v));memcpy(page,r+B2L_NC_DATA+4096,4096);
 for(uint64_t i=undo.count;i;i--){assert(!b2l_ncu_read(&undo,i-1,v));assert(b2l_nc_page_state(v,page,4096)>=0);memcpy(page,v+B2L_NC_DATA,4096);}
 assert(b2l_nc_u32(page)==0xd65f0fff&&b2l_nc_u32(page+4)==0xd503237f);
 puts("Undo archive: durable record/header publication, prefix/sync cuts, initialization recovery, identity binding and reverse-page restore passed");
}
