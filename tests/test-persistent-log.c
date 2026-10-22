/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b2l_log_core.h"
static struct b2l_log_ring concurrent;
static atomic_int running;
static void*publish_thread(void*arg){(void)arg;for(unsigned i=0;i<50000;i++){
 uint64_t pos=__atomic_fetch_add(&concurrent.next,1,__ATOMIC_RELAXED);
 if(!b2l_log_ring_publish(&concurrent,pos,(uint8_t)(pos%251)))__atomic_fetch_add(&concurrent.dropped,1,__ATOMIC_RELAXED);
 }return 0;}
static void*reader_thread(void*arg){(void)arg;while(atomic_load(&running)){
 uint64_t end=b2l_log_ring_end(&concurrent),start=end>B2L_LOG_TEXT_CAPACITY?end-B2L_LOG_TEXT_CAPACITY:0;
 for(uint64_t i=start;i<end;i++){uint8_t c=0;if(b2l_log_ring_read(&concurrent,i,&c))assert(c==(uint8_t)(i%251));}
 }return 0;}
static void*append_thread(void*arg){for(unsigned i=0;i<30000;i++)b2l_log_ring_append(&concurrent,(uint8_t)(uintptr_t)arg);return 0;}
struct disk{uint8_t*data;unsigned reads,writes,syncs;unsigned failread,failwrite,failsync;};
static int rd(void*ctx,uint64_t offset,void*out,size_t n){struct disk*d=ctx;assert(n==4096&&offset<=B2L_LOG_REGION_BYTES-n&&offset%4096==0);d->reads++;if(d->failread&&d->reads==d->failread)return 73;memcpy(out,d->data+offset,n);return 0;}
static int wr(void*ctx,uint64_t offset,const void*in,size_t n){struct disk*d=ctx;assert(n==4096&&offset<=B2L_LOG_REGION_BYTES-n&&offset%4096==0);d->writes++;if(d->failwrite&&d->writes==d->failwrite){memcpy(d->data+offset,in,31);return 74;}memcpy(d->data+offset,in,n);return 0;}
static int sy(void*ctx){struct disk*d=ctx;d->syncs++;return d->failsync&&d->syncs==d->failsync?75:0;}
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t*p){return u32(p)|(uint64_t)u32(p+4)<<32;}
int main(int argc,char**argv){
 struct b2l_log_ring ring={0};uint8_t c=0;
 assert(!b2l_log_ring_read(&ring,0,&c));
 for(uint64_t i=0;i<B2L_LOG_TEXT_CAPACITY*3+17;i++)b2l_log_ring_append(&ring,(uint8_t)(i%251));
 uint64_t end=b2l_log_ring_end(&ring);assert(end==B2L_LOG_TEXT_CAPACITY*3+17);
 assert(!b2l_log_ring_read(&ring,end-B2L_LOG_TEXT_CAPACITY-1,&c));
 for(uint64_t i=end-B2L_LOG_TEXT_CAPACITY;i<end;i++)assert(b2l_log_ring_read(&ring,i,&c)&&c==(uint8_t)(i%251));
 uint64_t newer=8*B2L_LOG_TEXT_CAPACITY;assert(b2l_log_ring_publish(&ring,newer,77));
 assert(!b2l_log_ring_publish(&ring,newer-B2L_LOG_TEXT_CAPACITY,12));
 assert(b2l_log_ring_read(&ring,newer,&c)&&c==77); /* delayed old writer cannot clobber */
 assert(!b2l_log_ring_publish(&ring,B2L_LOG_SEQUENCE_LIMIT,9));
 ring.next=B2L_LOG_SEQUENCE_LIMIT;b2l_log_ring_append(&ring,0);assert(ring.next==B2L_LOG_SEQUENCE_LIMIT&&b2l_log_ring_drops(&ring)==1);
 pthread_t writers[8],reader;atomic_store(&running,1);assert(!pthread_create(&reader,0,reader_thread,0));
 for(unsigned i=0;i<8;i++)assert(!pthread_create(&writers[i],0,publish_thread,0));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(writers[i],0));atomic_store(&running,0);assert(!pthread_join(reader,0));
 assert(b2l_log_ring_end(&concurrent)==400000);
 memset(&concurrent,0,sizeof concurrent);
 for(unsigned i=0;i<8;i++)assert(!pthread_create(&writers[i],0,append_thread,(void*)(uintptr_t)(i+1)));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(writers[i],0));uint64_t appended=b2l_log_ring_end(&concurrent);assert(appended>0&&appended<=240000);
 for(uint64_t i=appended-B2L_LOG_TEXT_CAPACITY;i<appended;i++)if(b2l_log_ring_read(&concurrent,i,&c))assert(c>=1&&c<=8);
 struct disk d={0};d.data=calloc(1,B2L_LOG_REGION_BYTES);assert(d.data);
 struct b2l_log_sink sink={rd,wr,sy,&d,B2L_LOG_REGION_BYTES};struct b2l_log_writer w={0};
 assert(b2l_log_writer_open(&w,&sink)==B2L_LOG_INVALID_HEADER&&!d.writes);
 b2l_log_make_header(d.data,4);b2l_log_make_header(d.data+4096,7);
 uint64_t epoch=0;assert(!b2l_log_parse_header(d.data,&epoch)&&epoch==4);
 for(unsigned i=0;i<4096;i+=37){d.data[i]^=1;assert(b2l_log_parse_header(d.data,&epoch));d.data[i]^=1;}
 sink.region_bytes--;assert(b2l_log_writer_open(&w,&sink)==B2L_LOG_BAD_ARGUMENT&&!d.writes);sink.region_bytes++;
 assert(!b2l_log_writer_open(&w,&sink)&&w.epoch==8&&d.writes==1&&d.syncs==1);
 assert(!b2l_log_parse_header(d.data,&epoch)&&epoch==8);
 memset(&ring,0,sizeof ring);for(unsigned i=0;i<31;i++)b2l_log_ring_append(&ring,(uint8_t)('a'+i%26));
 assert(!b2l_log_writer_drain(&w,&ring,2)&&w.cursor==31&&w.record_index==1);
 uint8_t*p=d.data+8192;assert(!memcmp(p,"B2LLOGPG",8)&&u64(p+16)==8&&u64(p+32)==0&&u32(p+40)==31&&u32(p+44)==31&&u32(p+48)==b2l_log_page_crc(p));
 for(unsigned i=0;i<31;i++)assert((p[64+i/8]&(1<<(i%8)))&&p[512+i]==(uint8_t)('a'+i%26));
 uint64_t oldcursor=w.cursor,oldindex=w.record_index;
 for(unsigned i=0;i<B2L_LOG_TEXT_CAPACITY*2;i++)b2l_log_ring_append(&ring,(uint8_t)(i%251));
 d.failwrite=d.writes+1;assert(b2l_log_writer_drain(&w,&ring,2)==74&&w.cursor==oldcursor&&w.record_index==oldindex);
 d.failwrite=0;d.failsync=d.syncs+1;assert(b2l_log_writer_drain(&w,&ring,2)==75&&w.cursor==oldcursor&&w.record_index==oldindex);
 d.failsync=0;assert(!b2l_log_writer_drain(&w,&ring,2)&&w.cursor==b2l_log_ring_end(&ring)&&w.overwritten>0);
 /* Hole: a reservation interrupted before publication is explicitly absent. */
 memset(&ring,0,sizeof ring);b2l_log_ring_append(&ring,'X');ring.next=2;w.cursor=0;
 assert(!b2l_log_writer_drain(&w,&ring,1));p=d.data+8192+((w.record_index-1)%B2L_LOG_RECORD_COUNT)*4096;
 assert(u32(p+40)==2&&u32(p+44)==1&&(p[64]&3)==1);
 /* Wrap disk slots and header CRC/epoch still separate new boot records. */
 w.record_index=B2L_LOG_RECORD_COUNT+3;w.cursor=0;
 assert(!b2l_log_writer_drain(&w,&ring,1));p=d.data+8192+3*4096;assert(u64(p+24)==B2L_LOG_RECORD_COUNT+3);
 struct b2l_log_writer next={0};assert(!b2l_log_writer_open(&next,&sink)&&next.epoch==9);
 assert(!b2l_log_writer_drain(&next,&ring,1));
 if(argc>1){FILE*f=fopen(argv[1],"wb");assert(f);assert(fwrite(d.data,1,B2L_LOG_REGION_BYTES,f)==B2L_LOG_REGION_BYTES);assert(!fclose(f));}
 free(d.data);puts("actual log core concurrency, wrap, framing, IO failures: PASS");return 0;
}
