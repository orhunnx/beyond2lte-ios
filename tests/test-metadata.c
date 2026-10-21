#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "b2l_amfi_metadata.h"
static uint8_t data[4096];static int fail;
static int rd(void*o,uint64_t p,void*out,size_t n){(void)o;if(fail||p>sizeof(data)||n>sizeof(data)-p)return -1;memcpy(out,data+p,n);return 0;}
static void le(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void be(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[3-i]=(uint8_t)(v>>(8*i));}
static void valid(unsigned base){memset(data,0,sizeof(data));uint8_t*p=data+base;le(p,0xfeedfacf);le(p+4,0x100000c);le(p+12,2);le(p+16,1);le(p+20,16);le(p+32,0x1d);le(p+36,16);le(p+40,256);le(p+44,128);p+=256;be(p,0xfade0cc0);be(p+4,48);be(p+8,1);be(p+12,5);be(p+16,20);be(p+20,0xfade7171);be(p+24,28);memcpy(p+28,"<plist><dict/></plist>",20);}
static int run(struct b2l_amfi_reader*r,uint64_t base){uint8_t out[65536];size_t n=123;int result=b2l_amfi_read_xml(r,base,out,sizeof(out),&n);if(result==0){assert(n==28);assert(out[0]==0xfa);}else assert(n==0);return result;}
static int virtual_rd(void*o,uint64_t at,void*out,size_t n){(void)o;uint8_t h[64]={0};uint64_t base=UINT64_MAX-64;if(at==0&&n==8){be(h,0xcafebabf);be(h+4,1);}else if(at==8&&n==32){be(h,0x100000c);be(h+8,(uint32_t)(base>>32));be(h+12,(uint32_t)base);be(h+20,64);}else if(at==base&&n==32){le(h,0xfeedfacf);le(h+4,0x100000c);le(h+12,2);le(h+16,1);le(h+20,16);}else if(at==base+32&&(n==8||n==16)){le(h,0x1d);le(h+4,16);le(h+8,0xfffffff0);le(h+12,32);}else return -1;memcpy(out,h,n);return 0;}
int main(void){struct b2l_amfi_reader r={sizeof(data),rd,NULL};valid(0);assert(run(&r,0)==0);
 fail=1;assert(run(&r,0)==B2L_AMFI_XML_IO);fail=0;
 valid(0);le(data+16,0);le(data+20,0);assert(run(&r,0)==B2L_AMFI_XML_ABSENT);
 valid(0);le(data+20,65537);assert(run(&r,0)==B2L_AMFI_XML_INVALID);
 valid(0);le(data+36,8);assert(run(&r,0)==B2L_AMFI_XML_INVALID);
 valid(0);le(data+40,4090);assert(run(&r,0)==B2L_AMFI_XML_INVALID);
 valid(0);be(data+256+16,19);assert(run(&r,0)==B2L_AMFI_XML_INVALID);
 valid(0);be(data+256+24,65537);assert(run(&r,0)==B2L_AMFI_XML_INVALID);
 valid(0);be(data+256+12,7);assert(run(&r,0)==B2L_AMFI_XML_ABSENT);
 valid(0);be(data+256+8,65);assert(run(&r,0)==B2L_AMFI_XML_INVALID);
 /* Two slots sharing/overlapping the XML payload are rejected. */
 valid(0);be(data+264,2);be(data+272,28);be(data+276,7);be(data+280,28);be(data+284,0xfade7171);be(data+288,20);assert(run(&r,0)==B2L_AMFI_XML_INVALID);
 valid(512);be(data,0xcafebabe);be(data+4,1);be(data+8,0x100000c);be(data+16,512);be(data+20,1024);assert(run(&r,512)==0);
 be(data+20,280);assert(run(&r,512)==B2L_AMFI_XML_INVALID);
 struct b2l_amfi_reader vr={UINT64_MAX,virtual_rd,NULL};assert(run(&vr,UINT64_MAX-64)==B2L_AMFI_XML_INVALID);
 /* Bounded mutation corpus, using only generated synthetic metadata. */
 uint32_t rng=7;for(unsigned i=0;i<20000;i++){valid(0);for(unsigned j=0;j<4;j++){rng=rng*1664525u+1013904223u;unsigned at=rng%384;rng=rng*1664525u+1013904223u;data[at]=(uint8_t)rng;}uint8_t out[65536];size_t n=999;int k=b2l_amfi_read_xml(&r,0,out,sizeof(out),&n);assert((k==0&&n<=65536)||(k!=0&&n==0));}
 puts("Actual unsigned metadata reader: bounds/fat/overlap/errors/20k mutations PASS");return 0;}
