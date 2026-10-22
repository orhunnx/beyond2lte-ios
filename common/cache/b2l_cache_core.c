/* SPDX-License-Identifier: BSD-2-Clause
 * Observed-instruction cache for the opt-in stripped-pointer development ABI.
 * This converter deliberately excludes PACGA, LDRA and unknown instructions.
 */
#include "b2l_cache_core.h"
uint32_t b2l_nc_u32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
uint64_t b2l_nc_u64(const uint8_t*p){return b2l_nc_u32(p)|(uint64_t)b2l_nc_u32(p+4)<<32;}
void b2l_nc_p32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
void b2l_nc_p64(uint8_t*p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
uint32_t b2l_nc_crc(const void*v,size_t n){const uint8_t*p=v;uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;}
int b2l_nc_native(uint32_t i,uint32_t*out){
 if(!out)return 0;
 if((i&0xffffe000u)==0xdac10000u||(i&0xffffe3e0u)==0xdac123e0u||
    (i&0xffffffe0u)==0xdac143e0u||(i&0xffffffe0u)==0xdac147e0u){*out=0xd503201fu;return 1;}
 switch(i){
 case 0xd50320ffu:case 0xd503211fu:case 0xd503215fu:case 0xd503219fu:case 0xd50321dfu:
 case 0xd503231fu:case 0xd503233fu:case 0xd503235fu:case 0xd503237fu:
 case 0xd503239fu:case 0xd50323bfu:case 0xd50323dfu:case 0xd50323ffu:*out=0xd503201fu;return 1;
 case 0xd65f0bffu:case 0xd65f0fffu:*out=0xd65f03c0u;return 1;
 default:break;}
 if((i&0xfffff800u)==0xd71f0800u||(i&0xfffff800u)==0xd73f0800u||
    (i&0xfffff81fu)==0xd61f081fu||(i&0xfffff81fu)==0xd63f081fu){
  unsigned rn=(i>>5)&31u;if(rn==31)return 0;
  *out=((i&0x200000u)?0xd63f0000u:0xd61f0000u)|(rn<<5);return 1;
 }
 if((i&0x3ffffc00u)==0x38bfc000u){*out=(i&0xc00003ffu)|0x08dffc00u;return 1;}
 return 0;
}
uint64_t b2l_nc_header_bytes(const uint8_t*h){return b2l_nc_u32(h+8)==1?b2l_nc_u32(h+12):b2l_nc_u64(h+36);}
uint32_t b2l_nc_header_used(const uint8_t*h){return b2l_nc_u32(h+8)==1?b2l_nc_u32(h+20):b2l_nc_u32(h+52);}
uint64_t b2l_nc_header_generation(const uint8_t*h){return b2l_nc_u32(h+8)==1?0:b2l_nc_u64(h+44);}
int b2l_nc_header(const uint8_t*h,const uint8_t id[12]){
 unsigned version=b2l_nc_u32(h+8);
 if(b2l_nc_u32(h+16)!=B2L_NC_COPY||memcmp(h+24,id,12)||
    b2l_nc_u32(h+60)!=b2l_nc_crc(h,60))return -1;
 if(version==1){
  if(memcmp(h,"B2LNC001",8)||b2l_nc_u32(h+12)!=B2L_NC_BYTES||b2l_nc_u32(h+20)!=B2L_NC_SLOTS)return -1;
  for(unsigned i=36;i<60;i++)if(h[i])return -1;
 }else if(version==2){
  uint64_t bytes=b2l_nc_u64(h+36);
  if(memcmp(h,"B2LNC002",8)||b2l_nc_u32(h+12)||bytes<B2L_NC_HEADER+B2L_NC_CELL||
     (bytes&4095)||((bytes-B2L_NC_HEADER)/B2L_NC_CELL)>0xffffffffULL||
     b2l_nc_u32(h+20)!=(bytes-B2L_NC_HEADER)/B2L_NC_CELL||
     b2l_nc_u32(h+52)>b2l_nc_u32(h+20)||!b2l_nc_u64(h+44)||b2l_nc_u32(h+56))return -1;
 }else return -1;
 for(unsigned i=64;i<4096;i++)if(h[i])return -1;
 return 0;
}
int b2l_nc_headers(const uint8_t*h,const uint8_t id[12],uint64_t bytes,const uint8_t**out){
 *out=NULL;int a=!b2l_nc_header(h,id)&&b2l_nc_header_bytes(h)==bytes;
 int b=!b2l_nc_header(h+4096,id)&&b2l_nc_header_bytes(h+4096)==bytes;
 if(!a&&!b)return -1;
 if(a&&b){
  if(b2l_nc_u32(h+8)!=b2l_nc_u32(h+4096+8))return -1;
  uint64_t ga=b2l_nc_header_generation(h),gb=b2l_nc_header_generation(h+4096);
  if(ga==gb&&memcmp(h,h+4096,4096))return -1;
  *out=ga>=gb?h:h+4096;
 }else *out=a?h:h+4096;
 return 0;
}
int b2l_nc_header_v2(uint8_t*h,const uint8_t id[12],uint64_t bytes,uint32_t used,uint64_t generation){
 if(bytes<B2L_NC_HEADER+B2L_NC_CELL||(bytes&4095)||(bytes-B2L_NC_HEADER)/B2L_NC_CELL>0xffffffffULL||
    used>(bytes-B2L_NC_HEADER)/B2L_NC_CELL||!generation)return -1;
 memset(h,0,4096);memcpy(h,"B2LNC002",8);b2l_nc_p32(h+8,2);b2l_nc_p32(h+16,B2L_NC_COPY);
 b2l_nc_p32(h+20,(uint32_t)((bytes-B2L_NC_HEADER)/B2L_NC_CELL));memcpy(h+24,id,12);
 b2l_nc_p64(h+36,bytes);b2l_nc_p64(h+44,generation);b2l_nc_p32(h+52,used);
 b2l_nc_p32(h+60,b2l_nc_crc(h,60));return b2l_nc_header(h,id);
}
static int zeros(const uint8_t*p,unsigned n){while(n--)if(*p++)return 0;return 1;}
int b2l_nc_record(const uint8_t*r){
 if(memcmp(r,"B2LNCREC",8)||!b2l_nc_u64(r+8)||!b2l_nc_u64(r+16))return -1;
 uint64_t size=b2l_nc_u64(r+24),off=b2l_nc_u64(r+32);
 unsigned len=b2l_nc_u32(r+60),kind=b2l_nc_u32(r+56),changes=0;
 if(!size||size>0x7fffffffffffffffULL||(off&4095)||off>=size||
 len!=(size-off>=4096?4096:(unsigned)(size-off))||(kind!=1&&kind!=2)||zeros(r+40,16)||
 !zeros(r+136,24)||!zeros(r+B2L_NC_DATA+2*4096,B2L_NC_COPY-B2L_NC_DATA-2*4096))return -1;
 uint8_t sum[32];const uint8_t*a=r+B2L_NC_DATA,*b=a+4096;
 if(!zeros(a+len,4096-len)||!zeros(b+len,4096-len))return -1;
 b2l_sha256(a,4096,sum);if(memcmp(sum,r+64,32))return -1;
 b2l_sha256(b,4096,sum);if(memcmp(sum,r+96,32))return -1;
 for(unsigned i=0;i<4096;i+=4){uint32_t x=b2l_nc_u32(a+i),y=b2l_nc_u32(b+i),z;
  if(x!=y){if(i+4>len||!b2l_nc_native(x,&z)||z!=y)return -1;changes++;}}
 if(!changes||changes!=b2l_nc_u32(r+132))return -1;
 /* CRC covers every byte, with the CRC field represented as zero. */
 uint32_t crc=~0u;for(unsigned i=0;i<B2L_NC_COPY;i++){crc^=(i>=128&&i<132)?0:r[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}
 return ~crc==b2l_nc_u32(r+128)?0:-1;
}
int b2l_nc_select(const uint8_t*c,const uint8_t**out){
 *out=0;int a=b2l_nc_record(c)==0,b=b2l_nc_record(c+B2L_NC_COPY)==0;
 if(a&&b&&b2l_nc_u64(c+8)==b2l_nc_u64(c+B2L_NC_COPY+8)&&memcmp(c,c+B2L_NC_COPY,B2L_NC_COPY))return -1;
 if(a&&(!b||b2l_nc_u64(c+8)>=b2l_nc_u64(c+B2L_NC_COPY+8)))*out=c;
 else if(b)*out=c+B2L_NC_COPY;
 return 0;
}
void b2l_nc_seal(uint8_t*r){
 uint32_t n=0;for(unsigned i=0;i<4096;i+=4)if(memcmp(r+B2L_NC_DATA+i,r+B2L_NC_DATA+4096+i,4))n++;
 b2l_nc_p32(r+132,n);b2l_sha256(r+B2L_NC_DATA,4096,r+64);b2l_sha256(r+B2L_NC_DATA+4096,4096,r+96);
 b2l_nc_p32(r+128,0);b2l_nc_p32(r+128,b2l_nc_crc(r,B2L_NC_COPY));
}
int b2l_nc_page_state(const uint8_t*r,const uint8_t*page,unsigned n){
 if(n!=b2l_nc_u32(r+60))return -1;
 const uint8_t*a=r+B2L_NC_DATA,*b=a+4096;
 if(!memcmp(page,b,n))return 1;
 if(!memcmp(page,a,n))return 0;
 /* Interrupted page write: only complete a mixture of durable old/new bytes. */
 for(unsigned i=0;i<n;i++)if(page[i]!=a[i]&&page[i]!=b[i])return -1;
 return 0;
}
