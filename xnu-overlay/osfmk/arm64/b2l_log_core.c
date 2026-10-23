/* SPDX-License-Identifier: BSD-2-Clause
 * Original bounded lossy console ring and framed raw-region writer.
 */
#include "b2l_log_core.h"
_Static_assert(__atomic_always_lock_free(8,0),"64-bit publication must be lock free");
_Static_assert(B2L_LOG_TEXT_CAPACITY>0,"ring cannot be empty");
_Static_assert(sizeof(((struct b2l_log_ring*)0)->slots[0])==8,"slot ABI");
static const uint8_t header_magic[8]={'B','2','L','L','O','G','0','1'};
static const uint8_t record_magic[8]={'B','2','L','L','O','G','P','G'};
static void zero(uint8_t*p,size_t n){while(n--)*p++=0;}
static void copy(uint8_t*p,const uint8_t*s,size_t n){while(n--)*p++=*s++;}
static int equal(const uint8_t*a,const uint8_t*b,size_t n){while(n--)if(*a++!=*b++)return 0;return 1;}
static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t*p){return (uint64_t)get32(p)|(uint64_t)get32(p+4)<<32;}
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void put64(uint8_t*p,uint64_t n){put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
uint64_t b2l_log_ring_end(const struct b2l_log_ring*r)
{uint64_t n=__atomic_load_n(&r->next,__ATOMIC_RELAXED);return n>B2L_LOG_SEQUENCE_LIMIT?B2L_LOG_SEQUENCE_LIMIT:n;}
uint64_t b2l_log_ring_drops(const struct b2l_log_ring*r)
{return __atomic_load_n(&r->dropped,__ATOMIC_RELAXED);}
int b2l_log_ring_publish(struct b2l_log_ring*r,uint64_t pos,uint8_t byte)
{
    if(!r||pos>=B2L_LOG_SEQUENCE_LIMIT)return 0;
    uint64_t seq=pos+1,packed=(seq<<8)|byte;
    uint64_t*slot=&r->slots[pos%B2L_LOG_TEXT_CAPACITY];
    uint64_t old=__atomic_load_n(slot,__ATOMIC_RELAXED);
    /* Bounded CAS also prevents a delayed old writer from replacing a newer
     * generation after the ring wraps. No allocation, locks or waiting. */
    for(unsigned tries=0;tries<4;tries++){
        if((old>>8)>=seq)return 0;
        if(__atomic_compare_exchange_n(slot,&old,packed,1,__ATOMIC_RELEASE,__ATOMIC_RELAXED))return 1;
    }
    return 0;
}
static void note_drop(struct b2l_log_ring*r)
{
    uint64_t old=__atomic_load_n(&r->dropped,__ATOMIC_RELAXED);
    for(unsigned tries=0;tries<4;tries++){
        if(old==UINT64_MAX)return;
        if(__atomic_compare_exchange_n(&r->dropped,&old,old+1,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return;
    }
    /* Diagnostic count is a lower bound if its own bounded CAS contends. */
}
void b2l_log_ring_append(struct b2l_log_ring*r,uint8_t byte)
{
    if(!r)return;
    uint64_t pos=__atomic_load_n(&r->next,__ATOMIC_RELAXED);
    /* Weak CAS prevents the compiler from hiding an unbounded LL/SC retry
     * inside one attempt on pre-LSE ARM. Allocation/publication both drop
     * after four attempts; interrupted writers never block other contexts. */
    for(unsigned tries=0;tries<4;tries++){
        if(pos>=B2L_LOG_SEQUENCE_LIMIT){note_drop(r);return;}
        if(__atomic_compare_exchange_n(&r->next,&pos,pos+1,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){
            if(!b2l_log_ring_publish(r,pos,byte))note_drop(r);
            return;
        }
    }
    note_drop(r);
}

/* One reservation keeps a whole diagnostic line contiguous even when ordinary
 * console bytes are published concurrently. Same bounded/no-wait semantics. */
void b2l_log_ring_append_bytes(struct b2l_log_ring*r,const void*data,size_t n)
{
    if(!r||!data||!n||n>4096)return;
    uint64_t pos=__atomic_load_n(&r->next,__ATOMIC_RELAXED);
    for(unsigned tries=0;tries<4;tries++){
        if(pos>B2L_LOG_SEQUENCE_LIMIT-n){note_drop(r);return;}
        if(__atomic_compare_exchange_n(&r->next,&pos,pos+n,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){
            const uint8_t*p=data;
            for(size_t i=0;i<n;i++)if(!b2l_log_ring_publish(r,pos+i,p[i]))note_drop(r);
            return;
        }
    }
    note_drop(r);
}

int b2l_log_ring_read(const struct b2l_log_ring*r,uint64_t pos,uint8_t*out)
{
    if(!r||!out||pos>=B2L_LOG_SEQUENCE_LIMIT)return 0;
    uint64_t word=__atomic_load_n(&r->slots[pos%B2L_LOG_TEXT_CAPACITY],__ATOMIC_ACQUIRE);
    if((word>>8)!=pos+1)return 0;
    *out=(uint8_t)word;return 1;
}
uint32_t b2l_log_page_crc(const uint8_t*p)
{
    uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<B2L_LOG_PAGE_BYTES;i++){
        uint8_t byte=(i>=48&&i<52)?0:p[i];crc^=byte;
        for(unsigned k=0;k<8;k++)crc=(crc>>1)^(UINT32_C(0xedb88320)&(0-(crc&1)));
    }
    return ~crc;
}
void b2l_log_make_header(uint8_t*p,uint64_t epoch)
{
    zero(p,B2L_LOG_PAGE_BYTES);copy(p,header_magic,8);put32(p+8,1);
    put32(p+12,B2L_LOG_PAGE_BYTES);put64(p+16,B2L_LOG_REGION_BYTES);
    put32(p+24,B2L_LOG_PAGE_BYTES);put32(p+28,B2L_LOG_RECORD_COUNT);
    put64(p+32,B2L_LOG_DATA_OFFSET);put64(p+40,epoch);put32(p+48,b2l_log_page_crc(p));
}
int b2l_log_parse_header(const uint8_t*p,uint64_t*epoch)
{
    if(!p||!epoch||!equal(p,header_magic,8)||get32(p+8)!=1||
       get32(p+12)!=B2L_LOG_PAGE_BYTES||get64(p+16)!=B2L_LOG_REGION_BYTES||
       get32(p+24)!=B2L_LOG_PAGE_BYTES||get32(p+28)!=B2L_LOG_RECORD_COUNT||
       get64(p+32)!=B2L_LOG_DATA_OFFSET||get32(p+48)!=b2l_log_page_crc(p))return B2L_LOG_INVALID_HEADER;
    for(unsigned i=52;i<B2L_LOG_PAGE_BYTES;i++)if(p[i])return B2L_LOG_INVALID_HEADER;
    *epoch=get64(p+40);return 0;
}
int b2l_log_writer_open(struct b2l_log_writer*w,const struct b2l_log_sink*s)
{
    if(!w||!s||!s->read||!s->write||!s->sync||s->region_bytes!=B2L_LOG_REGION_BYTES)return B2L_LOG_BAD_ARGUMENT;
    w->active=0;w->sink=*s;w->cursor=0;w->record_index=0;w->overwritten=0;
    uint64_t epoch[2]={0,0};int valid[2]={0,0};
    for(unsigned i=0;i<2;i++){
        int err=s->read(s->context,(uint64_t)i*B2L_LOG_PAGE_BYTES,w->page,B2L_LOG_PAGE_BYTES);
        if(!err&&!b2l_log_parse_header(w->page,&epoch[i]))valid[i]=1;
    }
    if(!valid[0]&&!valid[1])return B2L_LOG_INVALID_HEADER;
    unsigned chosen=valid[1]&&(!valid[0]||epoch[1]>epoch[0])?1:0;
    if(epoch[chosen]==UINT64_MAX)return B2L_LOG_EXHAUSTED;
    w->epoch=epoch[chosen]+1;b2l_log_make_header(w->page,w->epoch);
    int err=s->write(s->context,(uint64_t)(chosen^1)*B2L_LOG_PAGE_BYTES,w->page,B2L_LOG_PAGE_BYTES);
    if(err)return err;
    err=s->sync(s->context);if(err)return err;
    w->active=1;return 0;
}
int b2l_log_writer_drain(struct b2l_log_writer*w,const struct b2l_log_ring*r,unsigned limit)
{
    if(!w||!r||!w->active||!limit||limit>B2L_LOG_RECORD_COUNT)return B2L_LOG_BAD_ARGUMENT;
    uint64_t end=b2l_log_ring_end(r),cursor=w->cursor,index=w->record_index,lost=w->overwritten;
    uint64_t oldest=end>B2L_LOG_TEXT_CAPACITY?end-B2L_LOG_TEXT_CAPACITY:0;
    if(cursor<oldest){lost+=oldest-cursor;cursor=oldest;}
    if(cursor>=end)return 0;
    unsigned written=0;
    while(cursor<end&&written<limit){
        if(index==UINT64_MAX)return B2L_LOG_EXHAUSTED;
        unsigned span=(unsigned)((end-cursor)>B2L_LOG_RECORD_SPAN?B2L_LOG_RECORD_SPAN:end-cursor);
        uint8_t*p=w->page;zero(p,B2L_LOG_PAGE_BYTES);copy(p,record_magic,8);
        put32(p+8,1);put32(p+12,64);put64(p+16,w->epoch);put64(p+24,index);
        put64(p+32,cursor);put32(p+40,span);
        unsigned valid=0;
        for(unsigned i=0;i<span;i++)if(b2l_log_ring_read(r,cursor+i,p+512+i)){
            p[64+i/8]|=(uint8_t)(1u<<(i%8));valid++;
        }
        put32(p+44,valid);put64(p+56,b2l_log_ring_drops(r));
        put32(p+48,b2l_log_page_crc(p));
        uint64_t offset=B2L_LOG_DATA_OFFSET+(index%B2L_LOG_RECORD_COUNT)*B2L_LOG_PAGE_BYTES;
        int err=w->sink.write(w->sink.context,offset,p,B2L_LOG_PAGE_BYTES);if(err)return err;
        cursor+=span;index++;written++;
    }
    int err=w->sink.sync(w->sink.context);if(err)return err;
    w->cursor=cursor;w->record_index=index;w->overwritten=lost;return 0;
}
